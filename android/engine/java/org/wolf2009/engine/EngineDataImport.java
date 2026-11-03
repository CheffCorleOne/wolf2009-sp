// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.engine;

import android.content.Context;
import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.nio.file.Files;

// SAF document IDs remain opaque. The engine receives only private copies,
// never content:// strings treated as filesystem names. No retail extraction.
final class EngineDataImport {
    interface Notice { void update(String text); }
    private static final long MIB=1024L*1024;
    private static final long[] LIMITS={64*MIB,64*MIB};
    private static final String[] SOURCES={"assets.pk4","maps/game/empty/empty.mpk"};
    private static final String[] TARGETS={"fixtures/wolf-materials.bin","maps/game/empty/empty.mpk"};
    private final ContentResolver resolver;
    private final Uri tree;
    private final Map<String,List<Entry>> cache=new HashMap<>();
    private int entries, supportFiles;
    private long metadata, supportBytes;
    private static final class Entry {
        final Uri uri;final String name;final boolean directory;final long size;
        Entry(Uri u,String n,boolean d,long s){uri=u;name=n;directory=d;size=s;}
    }
    private EngineDataImport(Context context,Uri selected){resolver=context.getContentResolver();tree=selected;}
    private static void cancelled() throws InterruptedException {if(Thread.currentThread().isInterrupted())throw new InterruptedException("Импорт остановлен");}
    static File last(Context context) {
        String value=context.getSharedPreferences("engine",0).getString("sourceRoot",null);
        if(value==null)return null;
        try{
            File root=new File(value).getCanonicalFile();
            String parent=new File(context.getFilesDir(),"engine-runtime").getCanonicalPath()+File.separator;
            if(!root.getPath().startsWith(parent)||!new File(root,"owner.ready").isFile())return null;
            for(String target:TARGETS){File f=new File(root,"base/"+target);if(!f.isFile()||f.length()<4)return null;}
            return root;
        }catch(IOException failure){return null;}
    }
    private static File child(File parent,String relative) throws IOException {
        File file=new File(parent,relative).getCanonicalFile();
        if(!file.getPath().startsWith(parent.getCanonicalPath()+File.separator))throw new IOException("Некорректный приватный путь");
        File directory=file.getParentFile();
        if(!directory.isDirectory()&&!directory.mkdirs())throw new IOException("Не удалось создать приватный каталог");
        return file;
    }
    private static void collectCleanup(File root,File file,int depth,List<File> ordered) throws IOException {
        if(depth>16||ordered.size()>768||Files.isSymbolicLink(file.toPath()))throw new IOException("Cleanup boundary");
        String path=file.getCanonicalPath(),prefix=root.getCanonicalPath();
        if(!path.equals(prefix)&&!path.startsWith(prefix+File.separator))throw new IOException("Cleanup containment");
        if(file.isDirectory()){
            File[] children=file.listFiles();if(children==null||children.length>768)throw new IOException("Cleanup listing");
            for(File child:children)collectCleanup(root,child,depth+1,ordered);
        }
        ordered.add(file);
    }
    private static void cleanupNewRoot(Context context,File root) {
        try{
            File parent=new File(context.getFilesDir(),"engine-runtime").getCanonicalFile();
            File actual=root.getCanonicalFile();
            if(!actual.getParentFile().equals(parent)||!actual.getName().matches("source-[0-9a-f-]{36}")||Files.isSymbolicLink(root.toPath()))throw new IOException("Cleanup root boundary");
            List<File> ordered=new ArrayList<>();collectCleanup(actual,actual,0,ordered);
            // Validate the entire bounded tree before deleting anything. A link
            // or unexpected boundary leaves this failed import for inspection.
            for(File file:ordered)if(!file.delete()&&file.exists())Log.w("Wolf2009Engine","Unable to clean failed import child");
        }catch(IOException refused){Log.w("Wolf2009Engine","Failed import cleanup refused: "+refused.getMessage());}
    }
    private List<Entry> children(Uri directory) throws Exception {
        cancelled();String id=DocumentsContract.getDocumentId(directory);
        if(cache.containsKey(id))return cache.get(id);
        List<Entry> result=new ArrayList<>();
        Uri query=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);
        String[] columns={DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,DocumentsContract.Document.COLUMN_SIZE};
        try(Cursor rows=resolver.query(query,columns,null,null,null)){
            if(rows==null)throw new IOException("Провайдер не прочитал папку");
            while(rows.moveToNext()){
                cancelled();if(result.size()>=8192)throw new IOException("В папке больше 8192 записей");
                String doc=rows.getString(0),name=rows.getString(1),mime=rows.getString(2);
                if(doc==null||doc.length()>8192||name==null||name.length()>4096)throw new IOException("Некорректная запись провайдера");
                Uri uri=DocumentsContract.buildDocumentUriUsingTree(tree,doc);
                metadata+=2L*(doc.length()+name.length()+uri.toString().length());
                if(++entries>20000||metadata>8*MIB)throw new IOException("Превышен лимит списка документов");
                result.add(new Entry(uri,name,DocumentsContract.Document.MIME_TYPE_DIR.equals(mime),rows.isNull(3)?-1:rows.getLong(3)));
            }
        }
        cache.put(id,result);return result;
    }
    private Entry find(Uri directory,String name) throws Exception {
        Entry match=null;
        for(Entry e:children(directory))if(name.equalsIgnoreCase(e.name)){
            if(match!=null)throw new IOException("Неоднозначное имя: "+name);match=e;
        }
        return match;
    }
    private Uri directory(Uri parent,String name) throws Exception {Entry e=find(parent,name);return e!=null&&e.directory?e.uri:null;}
    private Entry resolve(Uri base,String relative) throws Exception {
        String[] parts=relative.split("/");Uri current=base;
        for(int i=0;i<parts.length;i++){
            Entry e=find(current,parts[i]);if(e==null||(i<parts.length-1)!=e.directory)return null;
            if(i==parts.length-1)return e;current=e.uri;
        }return null;
    }
    private Entry[] select() throws Exception {
        if(tree==null||tree.toString().length()>16384||!DocumentsContract.isTreeUri(tree))throw new IOException("Выберите папку SP/base своей игры");
        Uri selected=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));
        List<Uri> bases=new ArrayList<>();bases.add(selected);
        Uri base=directory(selected,"base");if(base!=null)bases.add(base);
        Uri sp=directory(selected,"SP");if(sp!=null){base=directory(sp,"base");if(base!=null)bases.add(base);}
        Entry[] complete=null;
        for(Uri candidate:bases){
            Entry[] found={resolve(candidate,SOURCES[0]),resolve(candidate,SOURCES[1])};
            if(found[0]!=null&&found[1]!=null){
                if(complete!=null)throw new IOException("Найдены две папки base; выберите нужную SP/base");complete=found;
            }
        }
        if(complete==null)throw new IOException("Нужны assets.pk4 и maps/game/empty/empty.mpk из оригинальной SP/base");
        return complete;
    }
    private void support(Context context,File root,String relative,int depth) throws Exception {
        if(depth>12)throw new IOException("Слишком глубокий каталог engine support");
        String source="engine-support/base"+(relative.isEmpty()?"":"/"+relative);
        String[] names=context.getAssets().list(source);
        if(names==null)throw new IOException("Не найдены служебные ресурсы движка");
        if(names.length>0){
            for(String name:names){
                cancelled();if(!name.matches("[A-Za-z0-9_.-]+")||name.equals(".")||name.equals(".."))throw new IOException("Некорректное имя engine support");
                support(context,root,relative.isEmpty()?name:relative+"/"+name,depth+1);
            }
        }else{
            if(relative.isEmpty()||relative.startsWith("maps/")||relative.equals("maps"))throw new IOException("Карты не должны входить в APK support");
            if(++supportFiles>256)throw new IOException("Превышен лимит engine support");
            File output=child(root,"base/"+relative);
            try(InputStream input=context.getAssets().open(source);FileOutputStream out=new FileOutputStream(output)){
                byte[] buffer=new byte[32768];int n;
                while((n=input.read(buffer))!=-1){supportBytes+=n;if(supportBytes>16*MIB)throw new IOException("Слишком большой engine support");out.write(buffer,0,n);}
            }
        }
    }
    static File copy(Context context,Uri tree,Notice notice) throws Exception {
        EngineDataImport job=new EngineDataImport(context,tree);
        notice.update("Проверка оригинальной SP/base…");Entry[] selected=job.select();long expected=0;
        for(int i=0;i<selected.length;i++){
            if(selected[i].size< -1||selected[i].size>LIMITS[i])throw new IOException("Размер файла вне лимита: "+SOURCES[i]);
            if(selected[i].size>0)expected+=selected[i].size;
        }
        File parent=new File(context.getFilesDir(),"engine-runtime");
        if(!parent.isDirectory()&&!parent.mkdirs())throw new IOException("Не удалось создать runtime каталог");
        if(parent.getUsableSpace()<expected+20*MIB)throw new IOException("Недостаточно места для приватной копии");
        File root=new File(parent,"source-"+UUID.randomUUID());
        if(!root.mkdir())throw new IOException("Не удалось создать новый каталог импорта");
        boolean complete=false;
        try{
        // A failed import never replaces a previous ready source set. Only a
        // fully copied set gets a marker and becomes the persisted selection.
        job.support(context,root,"",0);
        long aggregate=0,lastNotice=0;byte[] buffer=new byte[256*1024];
        for(int i=0;i<selected.length;i++){
            cancelled();long bytes=0;File output=child(root,"base/"+TARGETS[i]);
            try(InputStream input=context.getContentResolver().openInputStream(selected[i].uri);FileOutputStream out=new FileOutputStream(output)){
                if(input==null)throw new IOException("Провайдер не открыл "+SOURCES[i]);
                int count,empty=0;
                while((count=input.read(buffer))!=-1){
                    cancelled();if(count==0){if(++empty>16)throw new IOException("Провайдер не передаёт данные");continue;}empty=0;
                    bytes+=count;aggregate+=count;if(bytes>LIMITS[i])throw new IOException("Файл больше лимита: "+SOURCES[i]);out.write(buffer,0,count);
                    long now=SystemClock.uptimeMillis();if(now-lastNotice>=400){lastNotice=now;notice.update("Копирование "+(i+1)+"/2 · "+(aggregate/MIB)+" МиБ");}
                }
                out.getFD().sync();
            }
            if(bytes<4||(selected[i].size>=0&&bytes!=selected[i].size))throw new IOException("Файл пуст или изменился: "+SOURCES[i]);
            output.setReadOnly();
        }
        cancelled();File save=new File(root,"save");if(!save.mkdir())throw new IOException("Не удалось создать каталог сохранений");
        try(FileOutputStream marker=new FileOutputStream(new File(root,"owner.ready"))){marker.write("original-assets-and-empty\n".getBytes("UTF-8"));marker.getFD().sync();}
        android.content.SharedPreferences preferences=context.getSharedPreferences("engine",0);
        String previous=preferences.getString("sourceRoot",null);
        if(!preferences.edit().putString("sourceRoot",root.getCanonicalPath()).commit()){
            android.content.SharedPreferences.Editor restore=preferences.edit();
            if(previous==null)restore.remove("sourceRoot");else restore.putString("sourceRoot",previous);
            restore.commit();throw new IOException("Не удалось сохранить выбранную копию");
        }
        complete=true;return root;
        }finally{if(!complete)cleanupNewRoot(context,root);}
    }
}
