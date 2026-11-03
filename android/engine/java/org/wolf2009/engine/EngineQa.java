// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.engine;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

// Explicit debug-only actual-APK check on the ORIGINAL imported empty map.
// No synthetic scene, game-state setter, second game init or replacement frame loop.
final class EngineQa {
    private final EngineActivity activity;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final boolean quit;
    private boolean began,failed;
    private double[] initial,wall;
    private long[] beforeSurface;
    private EngineQa(EngineActivity owner,boolean close){activity=owner;quit=close;}
    static EngineQa create(EngineActivity owner,Intent intent){
        if((owner.getApplicationInfo().flags&ApplicationInfo.FLAG_DEBUGGABLE)==0||!intent.getBooleanExtra("engineQa",false))return null;
        Log.i("Wolf2009Engine","ENGINE_ANDROID_QA_ENABLED original_empty=1");
        EngineQa qa=new EngineQa(owner,intent.getBooleanExtra("engineQaQuit",false));
        qa.handler.postDelayed(()->{if(!qa.began)qa.check(false,"native_game_state_timeout");},40000);
        return qa;
    }
    void gameReady(){if(began)return;began=true;later(700,()->{EngineNative.queryView();later(400,this::move);});}
    private void later(long delay,Runnable action){handler.postDelayed(()->{if(!failed&&!activity.isDestroyed())action.run();},delay);}
    private boolean check(boolean condition,String message){if(condition)return true;failed=true;EngineNative.clearInput();Log.e("Wolf2009Engine","ENGINE_ANDROID_QA_FAIL "+message);return false;}
    private void move(){
        initial=EngineNative.view();if(!check(initial[0]>0&&Math.abs(initial[1])<2&&Math.abs(initial[2])<2,"original_spawn_view"))return;
        EngineNative.analog(true,0,1);later(2400,()->{EngineNative.analog(false,0,0);later(200,()->{EngineNative.queryView();later(400,this::look);});});
    }
    private void look(){
        wall=EngineNative.view();long[] state=EngineNative.stats();
        if(!check(wall[0]>initial[0]&&wall[1]>167&&wall[1]<169&&Math.abs(wall[2]-initial[2])<1&&state[2]>0,"native_player_wall"))return;
        Log.i("Wolf2009Engine","ENGINE_ANDROID_QA_WALL x="+wall[1]+" y="+wall[2]+" native_analog_dispatch="+state[2]);
        View view=activity.gameSurface();long now=SystemClock.uptimeMillis();float x=view.getWidth()*.5f,y=view.getHeight()*.5f;
        MotionEvent down=MotionEvent.obtain(now,now,MotionEvent.ACTION_DOWN,x,y,0),move=MotionEvent.obtain(now,now+16,MotionEvent.ACTION_MOVE,x+30,y+8,0),up=MotionEvent.obtain(now,now+32,MotionEvent.ACTION_UP,x+30,y+8,0);
        view.dispatchTouchEvent(down);view.dispatchTouchEvent(move);view.dispatchTouchEvent(up);down.recycle();move.recycle();up.recycle();
        later(300,()->{EngineNative.queryView();later(400,this::surface);});
    }
    private void surface(){
        double[] looked=EngineNative.view();beforeSurface=EngineNative.stats();
        if(!check(looked[0]>wall[0]&&Math.abs(looked[4]-wall[4])>.1&&beforeSurface[3]>0,"actual_touch_look"))return;
        EngineNative.pause();activity.gameSurface().setVisibility(View.INVISIBLE);
        later(400,()->{activity.gameSurface().setVisibility(View.VISIBLE);EngineNative.resume();later(1000,this::finish);});
    }
    private void finish(){
        long[] now=EngineNative.stats();
        if(!check((now[0]&2)!=0&&now[4]>beforeSurface[4]&&now[5]>=beforeSurface[5]+2,"surface_recreate_and_frame_progress"))return;
        Log.i("Wolf2009Engine","ENGINE_ANDROID_QA_OK native_player=1 analog=1 touch_look=1 pause_resume_api=1 surface_recreated=1");
        if(quit)later(300,EngineNative::requestQuit);
    }
    void destroy(){handler.removeCallbacksAndMessages(null);}
}
