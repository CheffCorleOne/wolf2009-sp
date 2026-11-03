// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.engine;

import android.content.Context;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

// Surface transport follows pinned Q3EView: native idTech4A++ owns EGL.
final class EngineSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    private final EngineActivity owner;
    private boolean attached;
    private int lookPointer=-1;
    private float previousX,previousY;
    EngineSurfaceView(Context context,EngineActivity activity){
        super(context);owner=activity;getHolder().addCallback(this);getHolder().setKeepScreenOn(true);
        setFocusable(true);setFocusableInTouchMode(true);
        setContentDescription("Игровой мир. Потяните для обзора; кнопки обзора доступны под сценой.");
    }
    @Override public void surfaceCreated(SurfaceHolder holder){attached=true;owner.surfaceCreated(holder.getSurface());}
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){owner.surfaceSize(Math.max(1,width),Math.max(1,height));}
    @Override public void surfaceDestroyed(SurfaceHolder holder){attached=false;lookPointer=-1;owner.surfaceDestroyed();}
    boolean ready(){return attached&&getHolder().getSurface().isValid()&&getWidth()>0&&getHeight()>0;}
    @Override public boolean onTouchEvent(MotionEvent event){
        if(!owner.acceptInput())return true;
        int action=event.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN){lookPointer=event.getPointerId(0);previousX=event.getX();previousY=event.getY();requestFocus();}
        else if(action==MotionEvent.ACTION_MOVE&&lookPointer>=0){
            int index=event.findPointerIndex(lookPointer);
            if(index>=0){float x=event.getX(index),y=event.getY(index);EngineNative.motion((x-previousX)*2.0f,(y-previousY)*2.0f);previousX=x;previousY=y;}
        }else if(action==MotionEvent.ACTION_POINTER_UP&&event.getPointerId(event.getActionIndex())==lookPointer)lookPointer=-1;
        else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){lookPointer=-1;if(action==MotionEvent.ACTION_UP)performClick();}
        return true;
    }
    @Override public boolean performClick(){super.performClick();return true;}
}
