// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.engine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// Launcher in a dedicated :engine process. The stock engine's Quit exits that
// process; it is deliberately never restarted inside an existing native heap.
public final class EngineActivity extends Activity {
    private static final int PICK_GAME=41;
    private static final int BACKGROUND=Color.rgb(13,21,28),TEXT=Color.rgb(232,240,245),MUTED=Color.rgb(184,202,214),ACCENT=Color.rgb(12,91,109);
    private final ExecutorService importer=Executors.newSingleThreadExecutor();
    private final Handler main=new Handler(Looper.getMainLooper());
    private final List<Button> gameButtons=new ArrayList<>();
    private final boolean[] held=new boolean[4];
    private EngineSurfaceView surface;
    private EngineCallbacks callbacks;
    private EngineQa qa;
    private TextView status,hint;
    private ProgressBar progress;
    private Button choose,start,exit;
    private File sourceRoot;
    private boolean loading,nativeStarted,foreground,quitting,game;
    private int width=1,height=1,lastState=-1;
    private float lookX,lookY;
    private final Runnable lookTick=new Runnable(){@Override public void run(){if(acceptInput()&&(lookX!=0||lookY!=0)){EngineNative.motion(lookX,lookY);main.postDelayed(this,32);}}};
    private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private TextView text(String value,int size,int color){TextView view=new TextView(this);view.setText(value);view.setTextSize(size);view.setTextColor(color);view.setPadding(dp(8),dp(4),dp(8),dp(4));return view;}
    private Button button(String value){Button button=new Button(this);button.setText(value);button.setTextSize(14);button.setTextColor(TEXT);button.setMinHeight(dp(48));button.setMinimumHeight(dp(48));button.setMinWidth(dp(48));button.setMinimumWidth(dp(48));button.setBackgroundTintList(ColorStateList.valueOf(ACCENT));return button;}
    private LinearLayout row(){LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER_VERTICAL);row.setPadding(dp(8),dp(4),dp(8),dp(4));return row;}
    private void add(LinearLayout row,Button button){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);if(row.getChildCount()>0)p.leftMargin=dp(8);row.addView(button,p);}
    private void message(String value){if(!isDestroyed()&&!value.contentEquals(status.getText()))status.setText(value);}
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);callbacks=new EngineCallbacks(this);sourceRoot=EngineDataImport.last(this);
        getWindow().setStatusBarColor(BACKGROUND);getWindow().setNavigationBarColor(BACKGROUND);
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(BACKGROUND);
        root.setOnApplyWindowInsetsListener((view,insets)->{
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());root.setPadding(bars.left,bars.top,bars.right,bars.bottom);}
            else root.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets;
        });
        root.addView(text("Wolf2009 SP · нативный движок",18,TEXT));
        LinearLayout files=row();choose=button("Выбрать SP/base");start=button("Запустить empty");exit=button("Выход");
        choose.setOnClickListener(v->pick());start.setOnClickListener(v->startEngine());exit.setOnClickListener(v->back());
        add(files,choose);add(files,start);add(files,exit);root.addView(files);
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);progress.setIndeterminate(true);progress.setVisibility(View.INVISIBLE);root.addView(progress,new LinearLayout.LayoutParams(-1,dp(4)));
        status=text(sourceRoot==null?"Выберите папку SP/base своей PC-копии. Нужны assets.pk4 и maps/game/empty/empty.mpk.":"Своя копия импортирована. Можно запустить empty.",14,TEXT);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);root.addView(status);
        FrameLayout viewport=new FrameLayout(this);surface=new EngineSurfaceView(this,this);viewport.addView(surface,new FrameLayout.LayoutParams(-1,-1));
        root.addView(viewport,new LinearLayout.LayoutParams(-1,0,1));
        hint=text("empty — проверка движка на исходной карте. Кампания пока недоступна.",13,MUTED);root.addView(hint);
        LinearLayout movement=row();
        movement(movement,"←","Идти влево",0);movement(movement,"Вперёд","Идти вперёд",1);movement(movement,"Назад","Идти назад",2);movement(movement,"→","Идти вправо",3);
        Button jump=button("Прыжок");jump.setContentDescription("Прыгнуть");gameButtons.add(jump);
        hold(jump,()->EngineNative.key(true,32,0),()->EngineNative.key(false,32,0));jump.setOnClickListener(v->{if(acceptInput()){EngineNative.key(true,32,0);EngineNative.key(false,32,0);}});add(movement,jump);root.addView(movement);
        LinearLayout looking=row();look(looking,"←","Повернуть взгляд влево",-14,0);look(looking,"→","Повернуть взгляд вправо",14,0);look(looking,"↑","Поднять взгляд",0,-14);look(looking,"↓","Опустить взгляд",0,14);root.addView(looking);
        setContentView(root);root.requestApplyInsets();buttons();qa=EngineQa.create(this,getIntent());
    }
    private void movement(LinearLayout row,String label,String description,int direction){
        Button button=button(label);button.setContentDescription(description);gameButtons.add(button);
        hold(button,()->{held[direction]=true;movement();},()->{held[direction]=false;movement();});
        button.setOnClickListener(v->{if(acceptInput()){held[direction]=true;movement();main.postDelayed(()->{held[direction]=false;movement();},120);}});add(row,button);
    }
    private void movement(){if(!nativeStarted)return;float x=(held[3]?1:0)-(held[0]?1:0),y=(held[1]?1:0)-(held[2]?1:0);float length=(float)Math.sqrt(x*x+y*y);if(length>1){x/=length;y/=length;}EngineNative.analog(x!=0||y!=0,x,y);}
    private void look(LinearLayout row,String label,String description,float x,float y){
        Button button=button(label);button.setContentDescription(description);gameButtons.add(button);
        hold(button,()->{lookX=x;lookY=y;main.removeCallbacks(lookTick);lookTick.run();},()->{lookX=0;lookY=0;main.removeCallbacks(lookTick);});
        button.setOnClickListener(v->{if(acceptInput())EngineNative.motion(x*4,y*4);});add(row,button);
    }
    private void hold(Button button,Runnable down,Runnable up){
        button.setOnTouchListener((view,event)->{
            int action=event.getActionMasked();if(action==MotionEvent.ACTION_DOWN){if(!acceptInput())return true;view.setPressed(true);down.run();return true;}
            if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){view.setPressed(false);up.run();return true;}return true;
        });
    }
    private void buttons(){choose.setEnabled(!loading&&!nativeStarted);start.setEnabled(sourceRoot!=null&&!loading&&!nativeStarted);exit.setEnabled(!quitting);for(Button button:gameButtons)button.setEnabled(acceptInput());}
    private void pick(){
        if(loading||nativeStarted)return;Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);startActivityForResult(intent,PICK_GAME);
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(request!=PICK_GAME||result!=RESULT_OK||data==null||data.getData()==null)return;
        final Uri tree=data.getData();loading=true;progress.setVisibility(View.VISIBLE);buttons();message("Проверка папки оригинальной игры…");
        importer.execute(()->{
            try{
                File imported=EngineDataImport.copy(this,tree,notice->main.post(()->{if(!isDestroyed()&&loading)message(notice);}));
                main.post(()->{if(isDestroyed())return;sourceRoot=imported;loading=false;progress.setVisibility(View.INVISIBLE);message("Оригинальные архивы скопированы. Запустите empty.");buttons();});
            }catch(Exception error){main.post(()->{if(isDestroyed())return;loading=false;progress.setVisibility(View.INVISIBLE);message("Импорт: "+(error.getMessage()==null?error.getClass().getSimpleName():error.getMessage()));buttons();});}
        });
    }
    void startEngine(){
        if(nativeStarted||loading||sourceRoot==null)return;
        if(!foreground||!surface.ready()){message("Дождитесь готовности игровой поверхности.");return;}
        message("Запуск исходной empty…");progress.setVisibility(View.VISIBLE);loading=true;buttons();
        // Same buffer-size policy as pinned Q3EView. Native Q3E API has no
        // live-resolution setter; Android scales this buffer if layout changes.
        surface.getHolder().setFixedSize(width,height);
        float refresh=getWindowManager().getDefaultDisplay().getRefreshRate();
        String error=EngineNative.start(getApplicationInfo().nativeLibraryDir,sourceRoot.getAbsolutePath(),new File(sourceRoot,"save").getAbsolutePath(),surface.getHolder().getSurface(),width,height,Math.round(refresh),callbacks);
        loading=false;
        if(!error.isEmpty()){progress.setVisibility(View.INVISIBLE);message("Запуск: "+error);buttons();return;}
        nativeStarted=true;surface.requestFocus();buttons();
    }
    void nativeState(int value){
        if(value==lastState)return;lastState=value;game=(value&2)!=0&&(value&16)==0&&(value&32)==0;
        if((value&8)!=0){message("Загрузка исходной карты…");progress.setVisibility(View.VISIBLE);}
        else if(game){message("empty загружена. Кнопки — ходьба, движение по сцене — обзор.");progress.setVisibility(View.INVISIBLE);}
        else if((value&16)!=0){message("Консоль движка");progress.setVisibility(View.INVISIBLE);}
        else if((value&32)!=0){message("Меню движка");progress.setVisibility(View.INVISIBLE);}
        buttons();
        if(game&&qa!=null)qa.gameReady();
    }
    boolean acceptInput(){return nativeStarted&&foreground&&game&&!quitting;}
    View gameSurface(){return surface;}
    void grabMouse(boolean grab){/* Touch remains app-local; never capture desktop input. */}
    void surfaceSize(int w,int h){width=w;height=h;if(qa!=null&&sourceRoot!=null&&!nativeStarted)main.post(this::startEngine);}
    void surfaceCreated(Surface next){if(nativeStarted&&!quitting)EngineNative.surface(next);}
    void surfaceDestroyed(){releaseInput();if(nativeStarted&&!quitting)EngineNative.surface(null);}
    private void releaseInput(){for(int i=0;i<held.length;i++)held[i]=false;lookX=lookY=0;main.removeCallbacks(lookTick);if(nativeStarted)EngineNative.clearInput();}
    private void back(){
        if(quitting)return;if(!nativeStarted){finish();return;}releaseInput();EngineNative.pause();
        new AlertDialog.Builder(this).setTitle("Выйти из уровня?").setMessage("Движок завершит этот запуск.")
            .setPositiveButton("Выйти",(d,which)->quit()).setNegativeButton("Продолжить",(d,which)->{if(foreground)EngineNative.resume();})
            .setOnCancelListener(d->{if(foreground)EngineNative.resume();}).show();
    }
    private void quit(){if(quitting)return;quitting=true;releaseInput();buttons();message("Завершение движка…");EngineNative.requestQuit();}
    @Override public void onBackPressed(){back();}
    @Override protected void onPause(){foreground=false;releaseInput();if(nativeStarted)EngineNative.pause();callbacks.pauseAudio();buttons();super.onPause();}
    @Override protected void onResume(){super.onResume();foreground=true;if(nativeStarted)EngineNative.resume();callbacks.resumeAudio();buttons();}
    @Override protected void onDestroy(){if(qa!=null)qa.destroy();importer.shutdownNow();main.removeCallbacksAndMessages(null);releaseInput();if(nativeStarted)EngineNative.requestQuit();callbacks.destroy();super.onDestroy();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(event.getKeyCode()==KeyEvent.KEYCODE_BACK)return super.dispatchKeyEvent(event);
        if(acceptInput()){
            int key=0;switch(event.getKeyCode()){
                case KeyEvent.KEYCODE_W:key='w';break;case KeyEvent.KEYCODE_S:key='s';break;case KeyEvent.KEYCODE_A:key='a';break;case KeyEvent.KEYCODE_D:key='d';break;
                case KeyEvent.KEYCODE_SPACE:key=32;break;case KeyEvent.KEYCODE_SHIFT_LEFT:case KeyEvent.KEYCODE_SHIFT_RIGHT:key=142;break;
                case KeyEvent.KEYCODE_ESCAPE:key=27;break;
            }
            if(key!=0){EngineNative.key(event.getAction()==KeyEvent.ACTION_DOWN,key,0);return true;}
        }return super.dispatchKeyEvent(event);
    }
}
