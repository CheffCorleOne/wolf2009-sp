# Bullet narrowphase subset

Upstream: [Bullet](https://github.com/bulletphysics/bullet3/tree/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb),
commit `d3afe76132fc76b0f9b2359cbaef47a3766e7fbb`. This reviewed subset contains
11 translation units, 41 headers and the upstream Zlib license. It provides
GJK/EPA and continuous convex collision; the dynamics world, platform host,
demos and tools are not included. The target uses double precision throughout.
The Wolf adapter owns source geometry, query policy and result qualification.

`UPSTREAM.json` retains the original URL, byte length and SHA-256 of every
upstream file. Local modifications must be explicitly recorded there and
marked in source. Preserve all upstream copyright and license notices.

The additional attribution in `btVoronoiSimplexSolver.cpp` is retained:

Parts of the source are "from the book Real-Time Collision Detection by
Christer Ericson, published by Morgan Kaufmann Publishers,
(c) 2005 Elsevier Inc."

The repository's GPL license applies to the separately authored Wolf adapter.
It does not replace the included upstream notices or claim authorship of
Bullet's algorithms.
