package jp.wavetagger.rec

import java.io.File
import org.json.JSONObject

/**
 * The recordings kept on the phone: `<name>.wav` with a small `<name>.json` beside it (what the history shows, and whether it has
 * reached Drive). Sent recordings are removed after [keepDays] days; nothing that is still waiting is ever removed.
 */
object Recordings {
    class Meta(
        val name: String, val seconds: Double, val channels: Int, val rate: Int, val bits: Int, val peak: Double,
        val source: String, val status: String, val createdAt: Long, val sentAt: Long, val recovered: Boolean,
    )

    lateinit var dir: File

    fun init(files: File) { dir = File(files, "rec").also { it.mkdirs() } }

    private fun metaFile(wav: File) = File(wav.parentFile, wav.name.removeSuffix(".wav") + ".json")

    fun write(wav: File, m: Meta) {
        val j = JSONObject()
            .put("name", m.name).put("seconds", m.seconds).put("channels", m.channels).put("rate", m.rate).put("bits", m.bits)
            .put("peak", m.peak).put("source", m.source).put("status", m.status).put("createdAt", m.createdAt)
            .put("sentAt", m.sentAt).put("recovered", m.recovered)
        val tmp = File(wav.parentFile, wav.name + ".json.tmp")
        tmp.writeText(j.toString())
        java.nio.file.Files.move(tmp.toPath(), metaFile(wav).toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING)
    }

    private fun read(wav: File): Meta? = try {
        val j = JSONObject(metaFile(wav).readText())
        Meta(j.getString("name"), j.getDouble("seconds"), j.getInt("channels"), j.getInt("rate"), j.getInt("bits"), j.optDouble("peak"),
            j.optString("source"), j.optString("status", "wait"), j.optLong("createdAt"), j.optLong("sentAt"), j.optBoolean("recovered"))
    } catch (_: Throwable) { null }

    /** Newest first. A WAV without a (readable) description is described from the file itself. */
    fun list(): List<Meta> = (dir.listFiles { f -> f.name.endsWith(".wav") } ?: emptyArray())
        .mapNotNull { read(it) ?: describe(it) }
        .sortedByDescending { it.createdAt }

    private fun describe(wav: File): Meta? {
        val start = WavWriter.dataOffset(wav)
        if (start < 0) return null
        return Meta(wav.name, 0.0, 0, 0, 0, 0.0, "", "wait", wav.lastModified(), 0L, false)
    }

    fun markSent(name: String, now: Long = System.currentTimeMillis()) {
        val wav = File(dir, name)
        val m = read(wav) ?: return
        write(wav, Meta(m.name, m.seconds, m.channels, m.rate, m.bits, m.peak, m.source, "done", m.createdAt, now, m.recovered))
    }

    /** Deletes a recording, but only one that is not waiting to be sent unless [force]. Returns whether it is gone. */
    fun delete(name: String, force: Boolean): Boolean {
        val wav = File(dir, File(name).name) // no path parts from the page
        val m = read(wav)
        if (!force && (m == null || m.status != "done")) return false
        metaFile(wav).delete()
        return wav.delete() || !wav.exists()
    }

    /** Removes recordings that were sent more than [keepDays] days ago. Returns how many. */
    fun cleanup(keepDays: Int, now: Long = System.currentTimeMillis()): Int {
        var n = 0
        for (m in list()) {
            if (m.status == "done" && m.sentAt > 0 && now - m.sentAt > keepDays * 86_400_000L && delete(m.name, false)) n++
        }
        return n
    }

    /** Repairs the `.part` files that a crash left behind (nothing is recording when this runs). Returns the new file names. */
    fun recover(): List<String> {
        val out = ArrayList<String>()
        for (part in dir.listFiles { f -> f.name.endsWith(".wav.part") } ?: emptyArray()) {
            val frames = WavWriter.recover(part)
            if (frames <= 0) { part.delete(); continue }
            var target = File(dir, part.name.removeSuffix(".wav.part") + "_recovered.wav")
            var k = 2
            while (target.exists()) target = File(dir, part.name.removeSuffix(".wav.part") + "_recovered$k.wav").also { k++ }
            if (!part.renameTo(target)) continue
            val head = java.io.RandomAccessFile(target, "r").use { f -> val b = ByteArray(36); f.readFully(b); java.nio.ByteBuffer.wrap(b).order(java.nio.ByteOrder.LITTLE_ENDIAN) }
            val ch = head.getShort(22).toInt()
            val rate = head.getInt(24)
            val bits = head.getShort(34).toInt()
            write(target, Meta(target.name, frames.toDouble() / rate, ch, rate, bits, 0.0, "", "wait", target.lastModified(), 0L, true))
            out.add(target.name)
        }
        return out
    }
}
