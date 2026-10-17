package name.boyle.chris.sgtpuzzles.backend

import android.content.Context
import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.GameView.DragMode.PREVENT
import name.boyle.chris.sgtpuzzles.GameView.DragMode.REVERT_OFF_SCREEN
import name.boyle.chris.sgtpuzzles.GameView.DragMode.REVERT_TO_START
import name.boyle.chris.sgtpuzzles.GameView.DragMode.UNMODIFIED
import name.boyle.chris.sgtpuzzles.config.PrefsConstants

sealed class BackendName(
    val sourceName: String,
    val displayName: String,
    val title: String,
    @param:DrawableRes val icon: Int,
    @param:StringRes val description: Int,
    @param:StringRes val controlsToast: Int,
    @param:StringRes val controlsToastNoArrows: Int,
    private val keyIcons: Map<String, Int>,
    @param:ColorRes val nightColours: Array<Int>
) {

    override fun toString() = sourceName

    val preferencesName = "backend/$sourceName"

    fun icon(context: Context) = ContextCompat.getDrawable(context, icon)!!

    fun keyIcon(k: String) = keyIcons[k.removePrefix("sym_key_")]

    /**
     * Should the game have arrows by default? Usually not.
     * This is for puzzles where the default preset has small squares, or diagonals are needed.
     * Note that if a hardware D-pad or trackball is present, these are ignored.
     */
    val isArrowsVisibleByDefault by lazy {
        this in setOf(BLACKBOX, FLOOD, INERTIA, MINES, PATTERN, SLANT, TRACKS)
    }

    /**
     * Is this a Latin-square puzzle? Currently only affects whether to offer
     * a preference that hides the "M" button that inserts all pencil marks.
     */
    val isLatin by lazy {
        this in setOf(KEEN, SOLO, TOWERS, UNDEAD, UNEQUAL, SOLO_PLUS, UNDEAD_PLUS, UNEQUAL_PLUS)
    }

    /** How to handle drag and revert for each game. */
    val dragMode by lazy {
        when (this) {
            // By default, and for games where a touch means move in that direction, allow a swipe
            // to do the same and don't try to revert anything: if the user tries to swipe in the
            // direction they want to move, it's better to have a move happen towards where they
            // started, so they learn, than to have nothing happen.
            CUBE, FIFTEEN, FILLING, MOSAIC, NET, TWIDDLE -> UNMODIFIED

            // For games where a drag can be canceled by going off-screen, do that.
            GALAXIES, MAP, MINES, PATTERN, PEGS, RECT, TENTS, TRACKS, UNTANGLE -> REVERT_OFF_SCREEN

            // Extra puzzles (extras-*) where dragging draws something (paths, lines, ranges).
            ASCENT, BOATS, BRICKS, CLUSTERS, FLOW, ROME, SPOKES, STICKS, WALLS -> REVERT_OFF_SCREEN

            // For games where dragging something off the board would be harmful, revert by
            // dragging back to the start of the drag instead.
            BRIDGES, GUESS, PEARL, SIGNPOST -> REVERT_TO_START

            // For many games with a cursor and no drag facilities, just ignore drags completely.
            // The reason we don't just enable one-finger pan in these games is it would be an
            // inconsistent UI since all the other games can't.
            BLACKBOX, DOMINOSA, FLIP, FLOOD, INERTIA, KEEN, LIGHTUP, LOOPY, MAGNETS, NETSLIDE,
            PALISADE, RANGE, SAMEGAME, SINGLES, SIXTEEN, SLANT, SOLO, TOWERS, UNDEAD, UNEQUAL,
            UNRULY,
            // Extra puzzles (extras-*) that are driven by taps / the cursor only.
            ABCD, ALPHACRYPT, FACTORCROSS, IDENTIFIER, KAKURO, MATHRAX, SALAD, SEISMIC, STELLAR,
            SUBSETS, SUPERMAZE, CROSSING, SOLO_PLUS, UNDEAD_PLUS, UNEQUAL_PLUS -> PREVENT
        }
    }

    /**
     * The extra puzzles (extras-*) have no hand-made night palette, so their colours are derived
     * automatically (see GameView.refreshColours).
     */
    val autoNightColours by lazy {
        this in setOf(
            ABCD, ALPHACRYPT, ASCENT, BOATS, BRICKS, CLUSTERS, CROSSING, FACTORCROSS, FLOW,
            IDENTIFIER, KAKURO, MATHRAX, ROME, SALAD, SEISMIC, SOLO_PLUS, SPOKES, STELLAR, STICKS,
            SUBSETS, SUPERMAZE, UNDEAD_PLUS, UNEQUAL_PLUS, WALLS
        )
    }

    /**
     * Colours (indices into the game's colour array) of the extra puzzles that make up pictures
     * (monsters, ships, planets, balls...). These keep their own hues at night instead of being
     * inverted like the grid, lines and text.
     */
    val nightKeepColours: Set<Int> by lazy {
        mapOf<BackendName, Set<Int>>(
            UNDEAD_PLUS to setOf(6, 7, 8),
            STELLAR to setOf(3, 4, 5, 6),
            BOATS to setOf(4, 5, 6, 7, 8, 9, 10),
            WALLS to setOf(5, 6, 7, 8, 9),
            FLOW to setOf(7, 8, 9),
            SALAD to setOf(7, 8, 9, 11, 12, 13, 16),
            SUPERMAZE to setOf(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32),
            CROSSING to setOf(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35),
            IDENTIFIER to setOf(2, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24)
        )[this] ?: emptySet()
    }

    private val swapLRName by lazy { PrefsConstants.SWAP_L_R_PREFIX + this }

    val swapLRNatively by lazy { this in SWAP_L_R_NATIVE_PREFS }

    fun getSwapLR(context: Context): Boolean {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        SWAP_L_R_NATIVE_PREFS[this]?.let { pref ->
            val nativeOn = GameEngineImpl.getPrefs(context, this)?.split("\n")
                ?.contains("${pref.kw}=${pref.onVal}")
            nativeOn?.let { return it }
            // Native pref not present; migrate from Android-specific name
            if (prefs.contains(swapLRName)) {
                val oldVal = prefs.getBoolean(swapLRName, false)
                putSwapLR(context, oldVal)
                prefs.edit { remove(swapLRName) }
                return oldVal
            }
        }
        return prefs.getBoolean(swapLRName, false)
    }

    fun putSwapLR(context: Context, swap: Boolean) {
        SWAP_L_R_NATIVE_PREFS[this]?.let { pref ->
            val otherLines = GameEngineImpl.getPrefs(context, this)?.split("\n")
                ?.filterNot { it.startsWith("${pref.kw}=") } ?: listOf()
            val newPrefs = "${otherLines.joinToString("\n")}${pref.kw}=${if (swap) pref.onVal else pref.offVal}\n"
            GameEngineImpl.savePrefs(context, this, newPrefs)
            return
        }
        PreferenceManager.getDefaultSharedPreferences(context).edit {
            putBoolean(swapLRName, swap)
        }
    }

    companion object {
        @JvmStatic
        val all: Set<BackendName> by lazy {
            BackendName::class.sealedSubclasses.map { it.objectInstance!! }.toSet()
        }

        private val BY_DISPLAY_NAME: Map<String, BackendName> by lazy {
            all.associateBy { it.displayName }
        }

        @UsedByJNI
        @JvmStatic
        fun byDisplayName(name: String?) = BY_DISPLAY_NAME[name]

        private val BY_LOWERCASE: Map<String, BackendName> by lazy {
            all.associateBy { it.sourceName }
        }

        @JvmStatic
        fun byLowerCase(name: String?) = BY_LOWERCASE[name]

        data class NativeSwapPref(val kw: String, val offVal: String, val onVal: String)

        private val SWAP_L_R_NATIVE_PREFS = mapOf(
            RANGE to NativeSwapPref("left-mouse-button", "fill", "dot"),
            SLANT to NativeSwapPref("left-button", "\\", "/"),
        )
    }
}
