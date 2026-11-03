/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2006 Erwin Coumans  https://bulletphysics.org

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

// Locally altered for Wolf2009: expose non-hit reasons and a strict opt-in
// iteration budget. The opt-in path uses linear origins and shortest quaternion
// slerp, without integrateTransform's angular clamp. Legacy bool callers preserve
// the upstream math/defaults and integration policy.

#include "btContinuousConvexCollision.h"
#include "BulletCollision/CollisionShapes/btConvexShape.h"
#include "BulletCollision/NarrowPhaseCollision/btSimplexSolverInterface.h"
#include "LinearMath/btTransformUtil.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"

#include "btGjkPairDetector.h"
#include "btPointCollector.h"
#include "BulletCollision/CollisionShapes/btStaticPlaneShape.h"

btContinuousConvexCollision::btContinuousConvexCollision(const btConvexShape* convexA, const btConvexShape* convexB, btSimplexSolverInterface* simplexSolver, btConvexPenetrationDepthSolver* penetrationDepthSolver)
	: m_simplexSolver(simplexSolver),
	  m_penetrationDepthSolver(penetrationDepthSolver),
	  m_convexA(convexA),
	  m_convexB1(convexB),
	  m_planeShape(0)
{
}

btContinuousConvexCollision::btContinuousConvexCollision(const btConvexShape* convexA, const btStaticPlaneShape* plane)
	: m_simplexSolver(0),
	  m_penetrationDepthSolver(0),
	  m_convexA(convexA),
	  m_convexB1(0),
	  m_planeShape(plane)
{
}

/// This maximum should not be necessary. It allows for untested/degenerate cases in production code.
/// You don't want your game ever to lock-up.
#define MAX_ITERATIONS 64

void btContinuousConvexCollision::computeClosestPoints(const btTransform& transA, const btTransform& transB, btPointCollector& pointCollector)
{
	if (m_convexB1)
	{
		m_simplexSolver->reset();
		btGjkPairDetector gjk(m_convexA, m_convexB1, m_convexA->getShapeType(), m_convexB1->getShapeType(), m_convexA->getMargin(), m_convexB1->getMargin(), m_simplexSolver, m_penetrationDepthSolver);
		btGjkPairDetector::ClosestPointInput input;
		input.m_transformA = transA;
		input.m_transformB = transB;
		gjk.getClosestPoints(input, pointCollector, 0);
	}
	else
	{
		//convex versus plane
		const btConvexShape* convexShape = m_convexA;
		const btStaticPlaneShape* planeShape = m_planeShape;

		const btVector3& planeNormal = planeShape->getPlaneNormal();
		const btScalar& planeConstant = planeShape->getPlaneConstant();

		btTransform convexWorldTransform = transA;
		btTransform convexInPlaneTrans;
		convexInPlaneTrans = transB.inverse() * convexWorldTransform;
		btTransform planeInConvex;
		planeInConvex = convexWorldTransform.inverse() * transB;

		btVector3 vtx = convexShape->localGetSupportingVertex(planeInConvex.getBasis() * -planeNormal);

		btVector3 vtxInPlane = convexInPlaneTrans(vtx);
		btScalar distance = (planeNormal.dot(vtxInPlane) - planeConstant);

		btVector3 vtxInPlaneProjected = vtxInPlane - distance * planeNormal;
		btVector3 vtxInPlaneWorld = transB * vtxInPlaneProjected;
		btVector3 normalOnSurfaceB = transB.getBasis() * planeNormal;

		pointCollector.addContactPoint(
			normalOnSurfaceB,
			vtxInPlaneWorld,
			distance);
	}
}

bool btContinuousConvexCollision::calcTimeOfImpact(
	const btTransform& fromA,
	const btTransform& toA,
	const btTransform& fromB,
	const btTransform& toB,
	CastResult& result)
{
	Diagnostics diagnostics;
	return calcTimeOfImpactStatusImpl(fromA, toA, fromB, toB, result,
		btScalar(0.001f), MAX_ITERATIONS, diagnostics, true) == DetailedHit;
}

btContinuousConvexCollision::DetailedStatus btContinuousConvexCollision::calcTimeOfImpactWithStatus(
	const btTransform& fromA, const btTransform& toA, const btTransform& fromB, const btTransform& toB,
	CastResult& result, btScalar distanceThreshold, int maximumIterations, Diagnostics& diagnostics)
{
	return calcTimeOfImpactStatusImpl(fromA, toA, fromB, toB, result,
		distanceThreshold, maximumIterations, diagnostics, false);
}

void btContinuousConvexCollision::interpolateLinearSlerp(
	const btTransform& from, const btTransform& to, btScalar fraction, btTransform& out,
	btVector3* linearVelocity, btVector3* angularVelocity)
{
	const btVector3 linear = to.getOrigin() - from.getOrigin();
	btQuaternion q0 = from.getRotation(), q1 = to.getRotation();
	q0.normalize();
	q1.normalize();
	// One sign rule for the exact-pi tie, angular velocity and interpolation.
	// atan2 retains tiny angles even when the scalar dot rounds to one.
	if (q0.dot(q1) < btScalar(0)) q1 = -q1;
	const btQuaternion delta = q1 * q0.inverse();
	const btVector3 vector(delta.x(), delta.y(), delta.z());
	const btScalar length = vector.length();
	const btScalar angle = btScalar(2) * btAtan2(length, btMax(btScalar(0), delta.w()));
	const btVector3 axis = length > btScalar(0) ? vector / length : btVector3(0, 0, 0);
	if (linearVelocity) *linearVelocity = linear;
	if (angularVelocity) *angularVelocity = axis * angle;
	out.setOrigin(from.getOrigin().lerp(to.getOrigin(), fraction));
	bool sameBasis = true;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			if (from.getBasis()[i][j] != to.getBasis()[i][j]) sameBasis = false;
	if (sameBasis && angularVelocity) angularVelocity->setZero();
	if (fraction == btScalar(0)) out.setOrigin(from.getOrigin());
	else if (fraction == btScalar(1)) out.setOrigin(to.getOrigin());
	if (fraction == btScalar(0) || sameBasis) out.setBasis(from.getBasis());
	else if (fraction == btScalar(1)) out.setBasis(to.getBasis());
	else
	{
		const btScalar half = angle * fraction * btScalar(.5);
		const btVector3 v = axis * btSin(half);
		out.setRotation(btQuaternion(v.x(), v.y(), v.z(), btCos(half)) * q0);
	}
}

btContinuousConvexCollision::DetailedStatus btContinuousConvexCollision::calcTimeOfImpactStatusImpl(
	const btTransform& fromA, const btTransform& toA, const btTransform& fromB, const btTransform& toB,
	CastResult& result, btScalar distanceThreshold, int maximumIterations, Diagnostics& diagnostics, bool legacyBudget)
{
	diagnostics = Diagnostics();
	if (!legacyBudget && (!(distanceThreshold > btScalar(0)) ||
		distanceThreshold >= BT_LARGE_FLOAT || maximumIterations < 1 || maximumIterations > 65536))
	{
		return diagnostics.status = DetailedInvalidOptions;
	}
	/// compute linear and angular velocity for this interval, to interpolate
	btVector3 linVelA, angVelA, linVelB, angVelB;
	if (legacyBudget)
	{
		btTransformUtil::calculateVelocity(fromA, toA, btScalar(1.), linVelA, angVelA);
		btTransformUtil::calculateVelocity(fromB, toB, btScalar(1.), linVelB, angVelB);
	}
	else
	{
		btTransform unused;
		interpolateLinearSlerp(fromA, toA, btScalar(0), unused, &linVelA, &angVelA);
		interpolateLinearSlerp(fromB, toB, btScalar(0), unused, &linVelB, &angVelB);
	}

	btScalar boundingRadiusA = m_convexA->getAngularMotionDisc();
	btScalar boundingRadiusB = m_convexB1 ? m_convexB1->getAngularMotionDisc() : 0.f;

	btScalar maxAngularProjectedVelocity = angVelA.length() * boundingRadiusA + angVelB.length() * boundingRadiusB;
	btVector3 relLinVel = (linVelB - linVelA);

	btScalar relLinVelocLength = (linVelB - linVelA).length();

	if ((relLinVelocLength + maxAngularProjectedVelocity) == 0.f)
		return diagnostics.status = DetailedNoMotion;

	btScalar lambda = btScalar(0.);

	btVector3 n;
	n.setValue(btScalar(0.), btScalar(0.), btScalar(0.));
	bool hasResult = false;
	btVector3 c;

	btScalar lastLambda = lambda;
	//btScalar epsilon = btScalar(0.001);

	int numIter = 0;
	//first solution, using GJK

	btScalar radius = distanceThreshold;
	//	result.drawCoordSystem(sphereTr);

	btPointCollector pointCollector1;

	{
		computeClosestPoints(fromA, fromB, pointCollector1);

		hasResult = pointCollector1.m_hasResult;
		c = pointCollector1.m_pointInWorld;
	}

	if (hasResult)
	{
		btScalar dist;
		dist = pointCollector1.m_distance + result.m_allowedPenetration;
		diagnostics.distance = dist;
		diagnostics.hasDistance = true;
		n = pointCollector1.m_normalOnBInWorld;
		btScalar projectedLinearVelocity = relLinVel.dot(n);
		if ((projectedLinearVelocity + maxAngularProjectedVelocity) <= SIMD_EPSILON)
			return diagnostics.status = DetailedNotApproaching;

		//not close enough
		while (dist > radius)
		{
			if (!legacyBudget && numIter >= maximumIterations)
			{
				result.reportFailure(-2, numIter);
				return diagnostics.status = DetailedIterationLimit;
			}
			if (result.m_debugDrawer)
			{
				result.m_debugDrawer->drawSphere(c, 0.2f, btVector3(1, 1, 1));
			}
			btScalar dLambda = btScalar(0.);

			projectedLinearVelocity = relLinVel.dot(n);

			//don't report time of impact for motion away from the contact normal (or causes minor penetration)
			if ((projectedLinearVelocity + maxAngularProjectedVelocity) <= SIMD_EPSILON)
				return diagnostics.status = DetailedNotApproaching;

			dLambda = dist / (projectedLinearVelocity + maxAngularProjectedVelocity);

			lambda += dLambda;
			diagnostics.fraction = lambda;

			if (lambda > btScalar(1.) || lambda < btScalar(0.))
				return diagnostics.status = DetailedOutsideInterval;

			//todo: next check with relative epsilon
			if (lambda <= lastLambda)
			{
				return diagnostics.status = DetailedNoProgress;
				//n.setValue(0,0,0);
				//break;
			}
			lastLambda = lambda;

			//interpolate to next lambda
			btTransform interpolatedTransA, interpolatedTransB, relativeTrans;

			if (legacyBudget)
			{
				btTransformUtil::integrateTransform(fromA, linVelA, angVelA, lambda, interpolatedTransA);
				btTransformUtil::integrateTransform(fromB, linVelB, angVelB, lambda, interpolatedTransB);
			}
			else
			{
				interpolateLinearSlerp(fromA, toA, lambda, interpolatedTransA);
				interpolateLinearSlerp(fromB, toB, lambda, interpolatedTransB);
			}
			relativeTrans = interpolatedTransB.inverseTimes(interpolatedTransA);

			if (result.m_debugDrawer)
			{
				result.m_debugDrawer->drawSphere(interpolatedTransA.getOrigin(), 0.2f, btVector3(1, 0, 0));
			}

			result.DebugDraw(lambda);

			btPointCollector pointCollector;
			computeClosestPoints(interpolatedTransA, interpolatedTransB, pointCollector);

			if (pointCollector.m_hasResult)
			{
				dist = pointCollector.m_distance + result.m_allowedPenetration;
				diagnostics.distance = dist;
				c = pointCollector.m_pointInWorld;
				n = pointCollector.m_normalOnBInWorld;
			}
			else
			{
				result.reportFailure(-1, numIter);
				return diagnostics.status = DetailedNoResult;
			}

			numIter++;
			diagnostics.iterations = numIter;
			if (legacyBudget && numIter > maximumIterations)
			{
				result.reportFailure(-2, numIter);
				return diagnostics.status = DetailedIterationLimit;
			}
		}

		result.m_fraction = lambda;
		result.m_normal = n;
		result.m_hitPoint = c;
		return diagnostics.status = DetailedHit;
	}

	return diagnostics.status = DetailedInitialNoResult;
}
