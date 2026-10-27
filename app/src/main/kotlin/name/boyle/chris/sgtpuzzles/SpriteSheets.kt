package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min

/**
 * Picture sheets for the extra graphics themes of Sokoban (the "Yoshi" skins). A sheet is a PNG in
 * the assets with 4 columns of 64 x 64 pixel cells; the game asks for a cell by number
 * (row * 4 + column). Every cell is scaled once to the exact size in device pixels it is drawn at
 * and kept, so that it is sharp at any board size (and drawing it again costs one blit).
 */
class SpriteSheets(private val context: Context) {
    private val sheets = arrayOfNulls<Bitmap>(FILES.size)
    private val failed = BooleanArray(FILES.size)
    private val cells = HashMap<Int, Bitmap>()
    private val scaled = HashMap<Long, Bitmap>()

    /** The cell scaled to [w] x [h] device pixels (at most [MAX_SIZE]; the caller stretches the rest). */
    fun get(sheet: Int, cell: Int, w: Int, h: Int): Bitmap? {
        if (w < 1 || h < 1) return null
        val pw = min(w, MAX_SIZE)
        val ph = min(h, MAX_SIZE)
        val key = ((sheet.toLong() * 64L + cell) * 4096L + pw) * 4096L + ph
        scaled[key]?.let { return it }
        val source = cellBitmap(sheet, cell) ?: return null
        val result = try {
            resample(source, pw, ph)
        } catch (e: OutOfMemoryError) {
            scaled.clear()
            return null
        }
        if (scaled.size >= MAX_CACHED) scaled.clear()
        scaled[key] = result
        return result
    }

    private fun cellBitmap(sheet: Int, cell: Int): Bitmap? {
        if (sheet !in FILES.indices || cell < 0 || cell >= COLUMNS * ROWS) return null
        val key = sheet * 64 + cell
        cells[key]?.let { return it }
        val sheetBitmap = sheets[sheet] ?: loadSheet(sheet) ?: return null
        val col = cell % COLUMNS
        val row = cell / COLUMNS
        if ((col + 1) * CELL > sheetBitmap.width || (row + 1) * CELL > sheetBitmap.height) return null
        val bitmap = Bitmap.createBitmap(sheetBitmap, col * CELL, row * CELL, CELL, CELL)
        cells[key] = bitmap
        return bitmap
    }

    private fun loadSheet(sheet: Int): Bitmap? {
        if (failed[sheet]) return null
        try {
            val options = BitmapFactory.Options().apply {
                inScaled = false
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            val bitmap = context.assets.open(FILES[sheet]).use {
                BitmapFactory.decodeStream(it, null, options)
            }
            if (bitmap != null) {
                sheets[sheet] = bitmap
                return bitmap
            }
        } catch (e: Exception) {
            Log.w(TAG, "Can't load ${FILES[sheet]}", e)
        }
        failed[sheet] = true
        return null
    }

    private fun resample(source: Bitmap, w: Int, h: Int): Bitmap {
        var current = source
        // Shrinking: halve (an exact 2 x 2 average, no aliasing) while that still leaves at least
        // the wanted size, then one smooth step to the exact size.
        while (current.width / 2 >= w && current.height / 2 >= h) {
            current = Bitmap.createScaledBitmap(current, current.width / 2, current.height / 2, true)
        }
        // Enlarging: first by a whole number with no smoothing, which keeps the edges of the
        // pixel art sharp, then smooth down to the exact size.
        if (current.width < w || current.height < h) {
            val k = ceil(max(w.toDouble() / current.width, h.toDouble() / current.height)).toInt()
            if (k > 1) {
                current = Bitmap.createScaledBitmap(current, current.width * k, current.height * k, false)
            }
        }
        if (current.width != w || current.height != h) {
            current = Bitmap.createScaledBitmap(current, w, h, true)
        }
        return current
    }

    companion object {
        private const val TAG = "SpriteSheets"
        private const val CELL = 64
        private const val COLUMNS = 4
        private const val ROWS = 8
        private const val MAX_SIZE = 256
        private const val MAX_CACHED = 48

        /** In the order of the skin numbers used by the game (Yoshi New, Classic+, Classic, Red). */
        private val FILES = arrayOf(
            "yoshi/yoshi-new.png",
            "yoshi/yoshi-classic-plus.png",
            "yoshi/yoshi-classic.png",
            "yoshi/yoshi-red.png"
        )
    }
}
