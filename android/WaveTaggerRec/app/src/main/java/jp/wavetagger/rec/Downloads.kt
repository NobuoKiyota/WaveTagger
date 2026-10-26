package jp.wavetagger.rec

import android.content.ContentValues
import android.content.Context
import android.provider.MediaStore

/**
 * Copies the recordings to `Download/WaveTagger/` (where the phone's file manager, Drive and a USB cable can reach them: the app's own
 * folder cannot be opened by anything else). A recording that is already there is left alone, so this is safe to call at any time.
 */
object Downloads {
    private const val FOLDER = "Download/WaveTagger/"

    @Synchronized
    fun copyMissing(context: Context): Int {
        val resolver = context.contentResolver
        val have = HashSet<String>()
        resolver.query(MediaStore.Downloads.EXTERNAL_CONTENT_URI, arrayOf(MediaStore.Downloads.DISPLAY_NAME),
            "${MediaStore.Downloads.RELATIVE_PATH} = ?", arrayOf(FOLDER), null)?.use { c ->
            while (c.moveToNext()) have.add(c.getString(0))
        }
        var copied = 0
        for (wav in Recordings.dir.listFiles { f -> f.name.endsWith(".wav") } ?: emptyArray()) {
            if (wav.name in have) continue
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, wav.name)
                put(MediaStore.Downloads.MIME_TYPE, "audio/wav")
                put(MediaStore.Downloads.RELATIVE_PATH, FOLDER)
                put(MediaStore.Downloads.IS_PENDING, 1)
            }
            val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: continue
            try {
                resolver.openOutputStream(uri)!!.use { out -> wav.inputStream().use { it.copyTo(out) } }
                resolver.update(uri, ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }, null, null)
                copied++
            } catch (_: Throwable) {
                resolver.delete(uri, null, null) // no half-copied file is left behind
            }
        }
        return copied
    }
}
