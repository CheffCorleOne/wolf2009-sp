// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.engine;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.net.Uri;
import android.os.Looper;
import android.view.PointerIcon;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

// Android-side implementation of all Q3E platform callbacks. The native engine
// retains its own rendering/input/audio choices; this is transport only.
public final class EngineCallbacks {
    private final EngineActivity activity;
    private volatile boolean destroyed;
    private volatile CountDownLatch dialogWait;
    private AlertDialog activeDialog;
    private AudioTrack audio;
    private ByteBuffer audioBuffer;
    EngineCallbacks(EngineActivity owner){activity=owner;}
    private void ui(Runnable action){activity.runOnUiThread(()->{if(!destroyed&&!activity.isFinishing())action.run();});}
    public void state(int value){ui(()->activity.nativeState(value));}
    public void grabMouse(boolean grab){ui(()->activity.grabMouse(grab));}
    public void smoothJoystick(boolean enabled){/* Native Sys_Analog remains the input authority. */}
    public void copyClipboard(String text){ui(()->((ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("Wolf2009",text)));}
    public String clipboard(){
        ClipboardManager manager=(ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip=manager.getPrimaryClip();if(clip==null||clip.getItemCount()==0)return "";
        CharSequence value=clip.getItemAt(0).coerceToText(activity);return value==null?"":value.toString();
    }
    public void openKeyboard(){ui(()->((InputMethodManager)activity.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(activity.gameSurface(),InputMethodManager.SHOW_IMPLICIT));}
    public void closeKeyboard(){ui(()->((InputMethodManager)activity.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(activity.gameSurface().getWindowToken(),0));}
    public void openUrl(String value){ui(()->{try{Uri uri=Uri.parse(value);if("http".equalsIgnoreCase(uri.getScheme())||"https".equalsIgnoreCase(uri.getScheme()))activity.startActivity(new Intent(Intent.ACTION_VIEW,uri));}catch(RuntimeException error){toast("Не удалось открыть ссылку");}});}
    public void finish(){activity.runOnUiThread(()->{if(!activity.isFinishing())activity.finish();});}
    public void showCursor(boolean visible){ui(()->activity.gameSurface().setPointerIcon(PointerIcon.getSystemIcon(activity,visible?PointerIcon.TYPE_ARROW:PointerIcon.TYPE_NULL)));}
    public void toast(String text){ui(()->Toast.makeText(activity,text,Toast.LENGTH_LONG).show());}
    public int dialog(String title,String message,String[] labels){
        if(destroyed||Looper.myLooper()==Looper.getMainLooper())return -1;
        CountDownLatch wait=new CountDownLatch(1);AtomicInteger answer=new AtomicInteger(-1);dialogWait=wait;
        ui(()->{
            if(destroyed){wait.countDown();return;}
            AlertDialog.Builder builder=new AlertDialog.Builder(activity).setTitle(title).setMessage(message).setOnCancelListener(d->{answer.set(0);wait.countDown();});
            if(labels!=null&&labels.length>0)builder.setItems(labels,(d,index)->{answer.set(index+1);wait.countDown();});
            else builder.setPositiveButton("Закрыть",(d,index)->{answer.set(0);wait.countDown();});
            activeDialog=builder.create();activeDialog.setOnDismissListener(d->wait.countDown());activeDialog.show();
        });
        try{while(!destroyed&&!wait.await(250,TimeUnit.MILLISECONDS)){} }catch(InterruptedException cancelled){Thread.currentThread().interrupt();}
        dialogWait=null;return answer.get();
    }
    public synchronized void audioInit(ByteBuffer buffer,int size){
        audioShutdown();if(buffer==null||size<=0)return;
        int minimum=AudioTrack.getMinBufferSize(44100,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT);
        audio=new AudioTrack(AudioManager.STREAM_MUSIC,44100,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT,Math.max(size,minimum),AudioTrack.MODE_STREAM);
        if(audio.getState()!=AudioTrack.STATE_INITIALIZED){audio.release();audio=null;return;}
        audioBuffer=buffer;audio.play();
    }
    public synchronized int audioWrite(int offset,int length){
        if(audio==null||audioBuffer==null)return 0;
        if(offset<0){audio.pause();audio.flush();audio.play();return 0;}
        if(length==Integer.MIN_VALUE)return 0;
        int count=Math.abs(length);if(offset>audioBuffer.capacity()||count>audioBuffer.capacity()-offset)return 0;
        ByteBuffer bytes=audioBuffer.duplicate();bytes.position(offset);bytes.limit(offset+count);
        int written=audio.write(bytes,count,AudioTrack.WRITE_BLOCKING);
        if(length<0){audio.pause();audio.flush();audio.play();}return Math.max(0,written);
    }
    public synchronized void audioShutdown(){if(audio!=null){audio.pause();audio.flush();audio.release();audio=null;}audioBuffer=null;}
    synchronized void pauseAudio(){if(audio!=null)audio.pause();}
    synchronized void resumeAudio(){if(audio!=null)audio.play();}
    void destroy(){destroyed=true;CountDownLatch wait=dialogWait;if(wait!=null)wait.countDown();if(activeDialog!=null)activeDialog.dismiss();audioShutdown();}
}
