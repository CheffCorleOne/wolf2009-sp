// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.viewer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.PopupMenu;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class ViewerActivity extends Activity {
    private static final int PICK_MPK = 2009;
    private static final int PICK_MATERIALS = 2010;
    private static final long MAX_MPK_BYTES = 512L * 1024 * 1024;
    private static final long MAX_MATERIAL_BYTES = 128L * 1024 * 1024;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status, hint;
    private ProgressBar progress;
    private Button open, materials, demo, mode, left, right, forward, backward, reset;
    private SceneView scene;
    private boolean loading, statusNotice;
    private int cameraMode;
    private final boolean[] held = new boolean[4];
    private boolean touchClick;

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setPadding(dp(16), dp(5), dp(16), dp(5)); return view;
    }
    private Button button(String label, Runnable action) {
        Button button = new Button(this); button.setText(label); button.setAllCaps(false); button.setTextSize(14);
        button.setPadding(dp(5),0,dp(5),0);
        button.setMinHeight(dp(48)); button.setMinimumHeight(dp(48)); button.setContentDescription(label);
        if (label.equals("+")) button.setContentDescription("Приблизить камеру");
        if (label.equals("−")) button.setContentDescription("Отдалить камеру");
        if (label.equals("Камера")) button.setContentDescription("Сбросить камеру");
        button.setTextColor(Color.rgb(226, 235, 242)); button.setBackgroundTintList(ColorStateList.valueOf(Color.rgb(37, 55, 70)));
        button.setOnClickListener(view -> action.run()); return button;
    }
    private void addButton(LinearLayout row, Button button, float weight) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(52), weight);
        params.setMargins(dp(3), 0, dp(3), 0); row.addView(button, params);
    }
    private LinearLayout row() { LinearLayout row = new LinearLayout(this); row.setPadding(dp(9), 0, dp(9), 0); return row; }
    private void gl(Runnable action) { scene.queueEvent(action); scene.requestRender(); }
    private void stopMotion() {
        for(int i=0;i<held.length;i++)held[i]=false;
        gl(NativeBridge::pauseMotion);
    }
    private void setCameraMode(int value) {
        stopMotion(); statusNotice=false;cameraMode=value;
        mode.setText(new String[]{"Орбита", "Полёт", "Осмотр", "Ходьба"}[value]);
        mode.setContentDescription("Выбрать режим камеры: "+mode.getText());
        left.setText(value==3?"Влево":"−"); right.setText(value==3?"Вправо":"+");
        left.setContentDescription(value==3?"Двигаться влево":"Отдалить камеру");
        right.setContentDescription(value==3?"Двигаться вправо":"Приблизить камеру");
        updateControls();
        hint.setText(value==3?"Палец — взгляд; удерживайте стрелки. Прототип ходьбы AABB, без прыжков и ступеней.":value==2?"Палец — взгляд из выбранной ENTS точки. Перемещение отключено.":"Один палец — поворот; два — сдвиг и масштаб.");
        scene.setRenderMode(value==3&&!loading?GLSurfaceView.RENDERMODE_CONTINUOUSLY:GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    }
    private void updateControls() {
        mode.setEnabled(!loading); reset.setEnabled(!loading);
        boolean translation=!loading&&cameraMode!=2;
        left.setEnabled(translation);right.setEnabled(translation);forward.setEnabled(translation);backward.setEnabled(translation);
    }
    private void cameraMenu() {
        stopMotion(); PopupMenu menu=new PopupMenu(this,mode);
        String[] labels={"Орбита","Свободная камера","Осмотр из ENTS…","Ходьба из ENTS…","Сведения о старте/коллизии"};
        for(int i=0;i<labels.length;i++)menu.getMenu().add(0,i,i,labels[i]);
        menu.setOnMenuItemClickListener(item->{int id=item.getItemId();
            if(id<2){setCameraMode(id);gl(()->NativeBridge.camera(id==1));}
            else if(id<4)chooseStart(id==3);
            else gl(()->{final String details=NativeBridge.startDetails();runOnUiThread(()->{if(!isDestroyed())new AlertDialog.Builder(this).setTitle("ENTS / HKXS").setMessage(details).setPositiveButton("Закрыть",null).show();});});
            return true;
        });menu.show();
    }
    private void chooseStart(boolean walking) {
        gl(()->{final String[] options=NativeBridge.startOptions();runOnUiThread(()->{
            if(isDestroyed()||loading)return;
            if(options==null||options.length<3){statusNotice=true;status.setText("Нет доступных ENTS стартов. Загрузите MPK и откройте сведения о старте.");return;}
            final long generation;try{generation=Long.parseLong(options[0]);}catch(NumberFormatException error){status.setText("Некорректное поколение сцены");return;}
            String[] labels=new String[(options.length-1)/2];for(int i=0;i<labels.length;i++)labels[i]=options[i*2+2];
            new AlertDialog.Builder(this).setTitle(walking?"Ходьба: выберите ENTS старт":"Осмотр: выберите ENTS старт")
                .setItems(labels,(dialog,index)->gl(()->{final String result=NativeBridge.enterStart(options[index*2+1],generation,walking);runOnUiThread(()->{
                    if(isDestroyed())return;boolean ok=result!=null&&result.startsWith("OK|");
                    if(ok)setCameraMode(walking?3:2);statusNotice=!ok;status.setText(ok?result.substring(3):result);
                });})).setNegativeButton("Отмена",null).show();
        });});
    }
    private void control(Button control,int index,Runnable normalAction,float horizontal,float ahead) {
        control.setOnClickListener(view->{if(touchClick)return;statusNotice=false;if(cameraMode==3)gl(()->NativeBridge.move(horizontal,ahead));else normalAction.run();});
        control.setOnTouchListener((view,event)->{
            if(cameraMode!=3||loading)return false;
            int action=event.getActionMasked();
            if(action==MotionEvent.ACTION_DOWN){held[index]=true;statusNotice=false;}
            else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL)held[index]=false;
            else return true;
            final float x=(held[1]?1:0)-(held[0]?1:0),y=(held[2]?1:0)-(held[3]?1:0);
            gl(()->NativeBridge.walkInput(x,y));
            view.setPressed(action==MotionEvent.ACTION_DOWN);
            if(action==MotionEvent.ACTION_UP){touchClick=true;view.performClick();touchClick=false;}
            return true;
        });
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(13, 21, 28)); getWindow().setNavigationBarColor(Color.rgb(13, 21, 28));
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setBackgroundColor(Color.rgb(13, 21, 28));
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                root.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else root.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.addView(text("Wolf2009 · геометрия", 22, Color.WHITE));
        root.addView(text("Просмотр статического мира. Кампания пока не реализована.", 14, Color.rgb(178, 195, 207)));
        LinearLayout files = row();
        open = button("Карта", () -> pick(PICK_MPK)); open.setContentDescription("Открыть карту MPK"); open.setBackgroundTintList(ColorStateList.valueOf(Color.rgb(12, 91, 109)));
        materials = button("Материалы", () -> pick(PICK_MATERIALS)); materials.setContentDescription("Импортировать материалы из assets.pk4");
        demo = button("Демо", () -> { if (!loading) { setCameraMode(0); gl(()->{NativeBridge.demo();NativeBridge.camera(false);NativeBridge.reset();}); } });
        reset = button("Камера", () -> {setCameraMode(0);gl(()->{NativeBridge.camera(false);NativeBridge.reset();});});
        addButton(files, open, 0.9f); addButton(files, materials, 1.5f); addButton(files, reset, 1.1f); addButton(files, demo, 0.9f); root.addView(files);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); progress.setIndeterminate(true); progress.setVisibility(View.INVISIBLE);
        root.addView(progress, new LinearLayout.LayoutParams(-1, dp(4)));
        status = text("Сначала «Материалы» → assets.pk4, затем «Карта» → MPK. Без материалов мир останется flat.", 14, Color.rgb(211, 225, 234));
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); status.setTextIsSelectable(true); root.addView(status);
        scene = new SceneView(); root.addView(scene, new LinearLayout.LayoutParams(-1, 0, 1));
        hint = text("Один палец — поворот; два — сдвиг и масштаб.", 14, Color.rgb(178, 195, 207)); root.addView(hint);
        LinearLayout camera = row();
        mode = button("Орбита", this::cameraMenu);
        addButton(camera, mode, 1.2f);
        left=button("−",()->{});right=button("+",()->{});forward=button("Вперёд",()->{});backward=button("Назад",()->{});
        control(left,0,()->gl(()->NativeBridge.gesture(0,0,1.2f,0,0)),-1,0);
        control(right,1,()->gl(()->NativeBridge.gesture(0,0,.8f,0,0)),1,0);
        control(forward,2,()->gl(()->NativeBridge.move(0,1)),0,1);
        control(backward,3,()->gl(()->NativeBridge.move(0,-1)),0,-1);
        addButton(camera,left,0.9f);addButton(camera,right,0.9f);addButton(camera,forward,1.1f);addButton(camera,backward,1.1f);
        root.addView(camera); setContentView(root); root.requestApplyInsets();
        NativeBridge.demo(); scene.requestRender();
    }

    private void pick(int request) {
        stopMotion();
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT); intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION); startActivityForResult(intent, request);
    }
    private void busy(boolean value) {
        loading = value; open.setEnabled(!value); materials.setEnabled(!value); demo.setEnabled(!value); progress.setVisibility(value ? View.VISIBLE : View.INVISIBLE);
        if(value)stopMotion();updateControls();scene.setRenderMode(cameraMode==3&&!value?GLSurfaceView.RENDERMODE_CONTINUOUSLY:GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if ((request != PICK_MPK && request != PICK_MATERIALS) || result != RESULT_OK || data == null || data.getData() == null || loading) return;
        final boolean importingMaterials=request==PICK_MATERIALS;
        final long maximumBytes=importingMaterials?MAX_MATERIAL_BYTES:MAX_MPK_BYTES;
        final Uri uri = data.getData(); busy(true); status.setText(importingMaterials?"Копирование PK4 в закрытый кэш приложения…":"Копирование MPK в закрытый кэш приложения…");
        worker.execute(() -> {
            File temporary = null;
            try {
                long declared = -1;
                try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.SIZE}, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) declared = cursor.getLong(0);
                }
                if (declared > maximumBytes) throw new IllegalArgumentException("Файл больше лимита "+(maximumBytes/(1024*1024))+" МиБ");
                temporary = File.createTempFile("viewer-import-", importingMaterials?".pk4":".mpk", getCacheDir());
                long total = 0, lastNotice = 0;
                try (InputStream input = getContentResolver().openInputStream(uri); FileOutputStream output = new FileOutputStream(temporary)) {
                    if (input == null) throw new IllegalArgumentException("Провайдер не открыл файл для чтения");
                    byte[] buffer = new byte[256 * 1024]; int count;
                    while ((count = input.read(buffer)) != -1) {
                        if (Thread.currentThread().isInterrupted()) throw new InterruptedException("Загрузка остановлена");
                        total += count; if (total > maximumBytes) throw new IllegalArgumentException("Файл больше лимита "+(maximumBytes/(1024*1024))+" МиБ");
                        output.write(buffer, 0, count);
                        long now = SystemClock.uptimeMillis();
                        if (now - lastNotice > 500) {
                            lastNotice = now; final String message = "Копирование: " + (total / (1024 * 1024)) + " МиБ";
                            runOnUiThread(() -> { if (!isDestroyed()) status.setText(message); });
                        }
                    }
                }
                if (total < 4) throw new IllegalArgumentException("Пустой или повреждённый файл");
                runOnUiThread(() -> { if (!isDestroyed()) status.setText(importingMaterials?"Проверка PK4 и разбор материалов…":"Разбор мира и подготовка diffuse DDS…"); });
                final String message = importingMaterials?NativeBridge.loadMaterials(temporary.getAbsolutePath()):NativeBridge.loadMpk(temporary.getAbsolutePath());
                runOnUiThread(() -> { if (!isDestroyed()) { busy(false); final boolean success=message.startsWith("OK|"); if(!importingMaterials&&success)setCameraMode(0);statusNotice=importingMaterials||!success;status.setText(success?message.substring(3):message); if(!importingMaterials&&success)gl(NativeBridge::requestStatus);else scene.requestRender(); } });
            } catch (Exception error) {
                final String message = "Не удалось импортировать файл: " + error.getMessage();
                runOnUiThread(() -> { if (!isDestroyed()) { busy(false);statusNotice=true; status.setText(message); } });
            } finally { if (temporary != null) temporary.delete(); }
        });
    }
    @Override protected void onPause() { stopMotion();super.onPause(); scene.onPause(); }
    @Override protected void onResume() { super.onResume(); if (scene != null) scene.onResume(); }
    @Override protected void onDestroy() { worker.shutdownNow(); super.onDestroy(); }

    private final class SceneView extends GLSurfaceView implements GLSurfaceView.Renderer {
        private int width = 1, height = 1;
        private float previousX, previousY;
        private int previousPointers;
        private final ScaleGestureDetector scale;
        SceneView() {
            super(ViewerActivity.this); setEGLContextClientVersion(3); setEGLConfigChooser(8,8,8,8,24,0); setPreserveEGLContextOnPause(true);
            setRenderer(this); setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
            setContentDescription("Трёхмерная сцена: поворот одним пальцем, сдвиг двумя, масштаб щипком");
            scale = new ScaleGestureDetector(ViewerActivity.this, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                @Override public boolean onScale(ScaleGestureDetector detector) {
                    final float zoom = 1.0f / detector.getScaleFactor(); gl(() -> NativeBridge.gesture(0,0,zoom,0,0)); return true;
                }
            });
        }
        @Override public void onSurfaceCreated(GL10 ignored, EGLConfig config) {
            final String error = NativeBridge.initialize();
            if (!error.isEmpty()) runOnUiThread(() -> status.setText("Ошибка GLES3: " + error));
        }
        @Override public void onSurfaceChanged(GL10 ignored, int w, int h) { width = Math.max(1,w); height = Math.max(1,h); }
        @Override public void onDrawFrame(GL10 ignored) {
            final String error=NativeBridge.draw(width,height);
            if (!error.isEmpty()) runOnUiThread(() -> {
                boolean renderStatus = error.startsWith("STATUS|");
                if (isDestroyed() || loading || (renderStatus && statusNotice)) return;
                String message = renderStatus ? error.substring(7) : "Ошибка отрисовки: " + error;
                if (!android.text.TextUtils.equals(status.getText(), message)) status.setText(message);
            });
        }
        @Override public boolean onTouchEvent(MotionEvent event) {
            if(loading)return true;
            if(event.getActionMasked()==MotionEvent.ACTION_DOWN)statusNotice=false;
            scale.onTouchEvent(event);
            final int pointers = event.getPointerCount(); float x = 0, y = 0;
            for (int i=0;i<pointers;i++) { x += event.getX(i); y += event.getY(i); } x/=pointers; y/=pointers;
            if (event.getActionMasked() == MotionEvent.ACTION_MOVE && pointers == previousPointers) {
                final float dx=(x-previousX)/Math.max(1,getWidth()), dy=(y-previousY)/Math.max(1,getHeight());
                if (pointers==1) gl(() -> NativeBridge.gesture(dx*6,dy*6,1,0,0));
                else gl(() -> NativeBridge.gesture(0,0,1,-dx,dy));
            }
            previousX=x; previousY=y; previousPointers=pointers;
            if(event.getActionMasked()==MotionEvent.ACTION_UP){previousPointers=0;performClick();} return true;
        }
        @Override public boolean performClick(){super.performClick();return true;}
    }
}
