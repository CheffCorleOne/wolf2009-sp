// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.viewer;

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
import java.util.HashSet;
import java.util.Set;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;

// Reads only the user-granted document tree. Provider document IDs are opaque:
// every descendant URI is built through DocumentsContract, never as a file path.
final class CampaignImport {
    private static final long MIB = 1024L * 1024;
    private static final int MAX_DIRECTORY_ENTRIES = 8192;
    private static final String STAGING_PREFIX = "campaign-import-";
    private static final Object STAGING_LOCK = new Object();
    private static final Set<Path> ACTIVE_STAGING = new HashSet<>();
    private static boolean staleStagingChecked;
    private static final String[] PATHS = {
        "assets.pk4", "maps/game/trainyard/trainyard.mpk",
        "maps/game/global/global.spk", "streampacks/scripted_voice_kc1_trn_vo_english.spk"
    };
    private static final long[] LIMITS = {128 * MIB, 512 * MIB, 128 * MIB, 32 * MIB};
    private static final long TOTAL_LIMIT = 800 * MIB;
    interface Notice { void update(String message); }
    static final class Staged implements AutoCloseable {
        final File directory;
        final File[] files = new File[PATHS.length];
        private boolean closed;
        Staged(File parent) throws IOException {
            directory = new File(parent, STAGING_PREFIX + UUID.randomUUID());
            synchronized (STAGING_LOCK) {
                if (!directory.mkdir()) throw new IOException("Не удалось создать закрытый кэш");
                ACTIVE_STAGING.add(directory.toPath().toAbsolutePath().normalize());
            }
            for (int i = 0; i < PATHS.length; i++) {
                String path = PATHS[i];
                files[i] = new File(directory, path.substring(path.lastIndexOf('/') + 1));
            }
        }
        @Override public void close() {
            synchronized (STAGING_LOCK) {
                if (closed) return;
                // Only four fixed children of this newly created private directory.
                for (File file : files) if (file != null) file.delete();
                directory.delete();
                ACTIVE_STAGING.remove(directory.toPath().toAbsolutePath().normalize());
                closed = true;
            }
        }
    }
    private static boolean stagingName(String name) {
        if (!name.startsWith(STAGING_PREFIX) || name.length() != STAGING_PREFIX.length() + 36) return false;
        String suffix = name.substring(STAGING_PREFIX.length());
        try { return UUID.fromString(suffix).toString().equals(suffix); }
        catch (IllegalArgumentException invalid) { return false; }
    }
    // Once per process, before its first import. Stages active in any Activity
    // are registered under the same lock. Never traverse a link, recurse into a
    // directory, or remove an unknown child; a failed delete is left for later.
    private static void cleanStaleStaging(File cache) throws IOException {
        synchronized (STAGING_LOCK) {
            if (staleStagingChecked) return;
            int inspected = 0, removed = 0, active = 0;
            try (DirectoryStream<Path> entries = Files.newDirectoryStream(cache.toPath(), STAGING_PREFIX + "*")) {
                for (Path entry : entries) {
                    if (inspected == 256) break;
                    inspected++;
                    Path path = entry.toAbsolutePath().normalize();
                    if (!stagingName(path.getFileName().toString()) ||
                        !Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(path)) continue;
                    if (ACTIVE_STAGING.contains(path)) { active++; continue; }
                    for (String relative : PATHS) {
                        Path child = path.resolve(relative.substring(relative.lastIndexOf('/') + 1));
                        try {
                            if (!Files.isDirectory(child, LinkOption.NOFOLLOW_LINKS)) Files.deleteIfExists(child);
                        } catch (IOException ignored) { /* Leave an inaccessible child; no recursive fallback. */ }
                    }
                    try { if (Files.deleteIfExists(path)) removed++; }
                    catch (IOException ignored) { /* Unknown children keep their containing directory intact. */ }
                }
            }
            staleStagingChecked = true;
            Log.i("Wolf2009", "Campaign staging cleanup inspected=" + inspected + " removed=" + removed + " activeSkipped=" + active);
        }
    }
    private static final class Entry {
        final Uri uri;
        final String name;
        final boolean directory;
        final long size;
        Entry(Uri uri, String name, boolean directory, long size) {
            this.uri = uri; this.name = name; this.directory = directory; this.size = size;
        }
    }
    private final ContentResolver resolver;
    private final Uri tree;
    private final Map<String, List<Entry>> directoryCache = new HashMap<>();
    private int totalEntries;
    private long metadataBytes;
    CampaignImport(ContentResolver resolver, Uri tree) {
        if (tree == null || tree.toString().length() > 16384) throw new IllegalArgumentException("Некорректная папка документов");
        this.resolver = resolver; this.tree = tree;
    }
    private static void cancelled() throws InterruptedException {
        if (Thread.currentThread().isInterrupted()) throw new InterruptedException("Загрузка остановлена");
    }
    private List<Entry> children(Uri directory) throws Exception {
        cancelled();
        String id = DocumentsContract.getDocumentId(directory);
        if (directoryCache.containsKey(id)) return directoryCache.get(id);
        Uri query = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE
        };
        List<Entry> entries = new ArrayList<>();
        try (Cursor cursor = resolver.query(query, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Провайдер не прочитал выбранную папку");
            while (cursor.moveToNext()) {
                cancelled();
                if (entries.size() >= MAX_DIRECTORY_ENTRIES) throw new IOException("В папке больше 8192 записей");
                String childId = cursor.getString(0), name = cursor.getString(1), mime = cursor.getString(2);
                if (childId == null || childId.length() > 8192 || name == null || name.length() > 4096)
                    throw new IOException("Некорректная запись системного провайдера");
                Uri childUri = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                metadataBytes += 2L * (childId.length() + name.length() + childUri.toString().length());
                if (++totalEntries > 20000 || metadataBytes > 8L * 1024 * 1024)
                    throw new IOException("Превышен лимит списка документов");
                entries.add(new Entry(childUri, name,
                    DocumentsContract.Document.MIME_TYPE_DIR.equals(mime), cursor.isNull(3) ? -1 : cursor.getLong(3)));
            }
        }
        directoryCache.put(id, entries);
        return entries;
    }
    private Entry find(Uri directory, String name) throws Exception {
        Entry found = null;
        for (Entry entry : children(directory)) if (name.equalsIgnoreCase(entry.name)) {
            if (found != null) throw new IOException("Неоднозначное имя: " + name);
            found = entry;
        }
        return found;
    }
    private Uri childDirectory(Uri directory, String name) throws Exception {
        Entry child = find(directory, name);
        return child != null && child.directory ? child.uri : null;
    }
    private Entry resolve(Uri base, String path) throws Exception {
        String[] parts = path.split("/");
        Uri directory = base;
        for (int i = 0; i < parts.length; i++) {
            Entry found = find(directory, parts[i]);
            if (found == null || (i < parts.length - 1) != found.directory) return null;
            if (i == parts.length - 1) return found;
            directory = found.uri;
        }
        return null;
    }
    private Entry[] selectBase() throws Exception {
        if (!DocumentsContract.isTreeUri(tree)) throw new IOException("Нужно выбрать папку SP/base");
        Uri selected = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        List<Uri> candidates = new ArrayList<>(); candidates.add(selected);
        Uri direct = childDirectory(selected, "base"); if (direct != null) candidates.add(direct);
        Uri sp = childDirectory(selected, "SP");
        if (sp != null) { Uri nested = childDirectory(sp, "base"); if (nested != null) candidates.add(nested); }
        Entry[] complete = null;
        String missing = PATHS[0]; int best = -1;
        for (Uri base : candidates) {
            Entry[] entries = new Entry[PATHS.length]; int matched = 0;
            for (int i = 0; i < PATHS.length; i++) {
                entries[i] = resolve(base, PATHS[i]);
                if (entries[i] != null) matched++;
            }
            if (matched > best) {
                best = matched;
                for (int i = 0; i < entries.length; i++) if (entries[i] == null) { missing = PATHS[i]; break; }
            }
            if (matched == PATHS.length) {
                if (complete != null) throw new IOException("Найдены две папки base; выберите нужную SP/base");
                complete = entries;
            }
        }
        if (complete == null) throw new IOException("В SP/base не найден " + missing);
        return complete;
    }
    Staged copy(File cache, Notice notice) throws Exception {
        cleanStaleStaging(cache);
        notice.update("Проверка папки SP/base…");
        Entry[] entries = selectBase(); long expected = 0;
        for (int i = 0; i < entries.length; i++) {
            if (entries[i].size < -1) throw new IOException("Некорректный размер: " + PATHS[i]);
            if (entries[i].size > LIMITS[i]) throw new IOException("Файл больше лимита: " + PATHS[i]);
            if (entries[i].size > 0) expected += entries[i].size;
        }
        if (expected > TOTAL_LIMIT || (cache.getUsableSpace() > 0 && cache.getUsableSpace() < expected + 8 * MIB))
            throw new IOException("Недостаточно места для временного импорта");
        Staged staged = new Staged(cache); boolean success = false; long aggregate = 0, lastNotice = 0;
        try {
            byte[] buffer = new byte[256 * 1024];
            for (int i = 0; i < entries.length; i++) {
                cancelled(); long total = 0;
                try (InputStream input = resolver.openInputStream(entries[i].uri);
                     FileOutputStream output = new FileOutputStream(staged.files[i])) {
                    if (input == null) throw new IOException("Не удалось прочитать " + PATHS[i]);
                    int count, emptyReads = 0;
                    while ((count = input.read(buffer)) != -1) {
                        cancelled();
                        if (count == 0) { if (++emptyReads > 16) throw new IOException("Провайдер не передаёт данные"); continue; }
                        emptyReads = 0; total += count; aggregate += count;
                        if (total > LIMITS[i] || aggregate > TOTAL_LIMIT) throw new IOException("Превышен лимит импорта: " + PATHS[i]);
                        output.write(buffer, 0, count);
                        long now = SystemClock.uptimeMillis();
                        if (now - lastNotice >= 500) {
                            lastNotice = now;
                            notice.update("Копирование " + (i + 1) + "/4: " + entries[i].name + " · " + (aggregate / MIB) + " МиБ");
                        }
                    }
                }
                if (total < 4 || (entries[i].size >= 0 && total != entries[i].size))
                    throw new IOException("Файл пуст или изменился при чтении: " + PATHS[i]);
            }
            cancelled(); success = true; return staged;
        } finally { if (!success) staged.close(); }
    }
}
