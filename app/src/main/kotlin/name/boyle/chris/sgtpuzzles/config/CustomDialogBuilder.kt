package name.boyle.chris.sgtpuzzles.config

import android.content.Context
import android.content.DialogInterface
import android.text.InputType.TYPE_CLASS_NUMBER
import android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
import android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
import android.view.View
import android.widget.AdapterView
import android.widget.CheckBox
import android.widget.ScrollView
import android.widget.TableRow
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.widget.AppCompatSpinner
import androidx.appcompat.widget.AppCompatTextView
import name.boyle.chris.sgtpuzzles.launch.GameLaunch
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.fromSeed
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.ofGameID
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.toGenerate
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Origin
import name.boyle.chris.sgtpuzzles.R
import name.boyle.chris.sgtpuzzles.backend.BLACKBOX
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.SOKOBAN
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.UsedByJNI
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder.Event.CFG_DESC
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder.Event.CFG_SEED
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder.Event.CFG_SETTINGS
import name.boyle.chris.sgtpuzzles.intValue
import kotlin.math.sqrt

/** Wraps views from ConfigViewsBuilder in a dialog and applies game-specific alterations. */
class CustomDialogBuilder private constructor(
    private val dialogBuilder: AlertDialog.Builder,
    private val engine: ConfigBuilder.EngineCallbacks,
    private val activity: ConfigBuilder.ActivityCallbacks,
    private val dialogEvent: ConfigBuilder.Event,
    private val backend: BackendName
) : ConfigViewsBuilder(dialogBuilder.context, engine) {

    // Make dialogBuilder first in order to pass the themed context it creates to our superclass
    @UsedByJNI
    constructor(
        nonThemedContext: Context,
        engine: ConfigBuilder.EngineCallbacks,
        activity: ConfigBuilder.ActivityCallbacks,
        dialogEvent: Int,
        backend: BackendName
    ) : this(AlertDialog.Builder(nonThemedContext), engine, activity,
        ConfigBuilder.Event.fromJNI(dialogEvent), backend)

    init {
        dialogBuilder.apply {
            setView(ScrollView(this.context).apply { addView(table) })
            setOnCancelListener { engine.configCancel() }
            setPositiveButton(android.R.string.ok) { dialog: DialogInterface, _: Int ->
                configApply()
                try {
                    val launch: GameLaunch = when (dialogEvent) {
                        CFG_DESC -> ofGameID(
                            backend, engine.fullGameIDFromDialog, Origin.CUSTOM_DIALOG
                        )

                        CFG_SEED -> fromSeed(
                            backend, engine.fullSeedFromDialog
                        )

                        else -> toGenerate(
                            backend, engine.configOK(), Origin.CUSTOM_DIALOG
                        )
                    }
                    activity.startGame(launch)
                    dialog.dismiss()
                } catch (e: IllegalArgumentException) {
                    activity.customDialogError(e.message)
                }
            }
            if (dialogEvent == CFG_SETTINGS) {
                setNegativeButton(R.string.Game_ID_) { _: DialogInterface?, _: Int ->
                    engine.configCancel()
                    engine.configEvent(
                        activity, CFG_DESC.jni, context, backend
                    )
                }
                setNeutralButton(R.string.Seed_) { _: DialogInterface?, _: Int ->
                    engine.configCancel()
                    engine.configEvent(
                        activity, CFG_SEED.jni, context, backend
                    )
                }
            }
        }
    }

    override fun setTitle(title: String) {
        dialogBuilder.setTitle(title)
    }

    // Sokoban's custom game dialog lists the settings of all the level generators; the rows of the
    // generators that are not selected are hidden, and this changes as soon as the choice does.
    private val generatorRows = mutableListOf<Pair<Int, View>>()
    private var generatorChoice: AppCompatSpinner? = null

    private val isSokobanSettings get() = backend === SOKOBAN && dialogEvent == CFG_SETTINGS

    /** 1.. for the rows of the level generators 1.. (Boulders, MCTS, ...), 0 for any other row. */
    private fun generatorOfRow(name: String) =
        SOKOBAN_GENERATOR_ROW_PREFIXES.indexOf(name.substringBefore(':')) + 1

    private fun showSelectedGeneratorRows() {
        val selected = generatorChoice?.selectedItemPosition ?: return
        for ((generator, view) in generatorRows) {
            view.visibility = if (generator == selected) View.VISIBLE else View.GONE
        }
    }

    override fun choicesAdded(name: String, row: TableRow, spinner: AppCompatSpinner) {
        if (!isSokobanSettings) return
        val generator = generatorOfRow(name)
        if (generator > 0) {
            generatorRows += generator to row
        } else if (name == SOKOBAN_GENERATOR_CHOICE) {
            generatorChoice = spinner
            spinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(
                    parent: AdapterView<*>?, view: View?, position: Int, id: Long
                ) {
                    showSelectedGeneratorRows()
                }

                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }
        }
    }

    @UsedByJNI
    override fun addString(whichEvent: Int, kw: String, name: String, value: String) {
        super.addStringView(whichEvent, name, value).apply {
            if (isSokobanSettings) {
                generatorOfRow(name).takeIf { it > 0 }?.let { generatorRows += it to row }
            }
            if (dialogEvent == CFG_SEED && value.indexOf('#') == value.length - 1) {
                table.addView(AppCompatTextView(table.context).apply { setText(R.string.seedWarning) })
            }
            when (name) {
                COLUMNS_OF_SUB_BLOCKS -> {
                    label.tag = COLS_LABEL_TAG
                    editText.tag = COLS_EDITTEXT_TAG
                }

                ROWS_OF_SUB_BLOCKS -> {
                    row.tag = ROWS_ROW_TAG
                    editText.tag = ROWS_EDITTEXT_TAG
                }
            }
            // Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
            // Uglier temporary-er hack: Black Box must accept a range for ball count.
            if (whichEvent == CFG_SETTINGS.jni && backend !== BLACKBOX) {
                editText.inputType =
                    (TYPE_CLASS_NUMBER or TYPE_NUMBER_FLAG_DECIMAL or TYPE_NUMBER_FLAG_SIGNED)
            }
        }
    }

    @UsedByJNI
    override fun addBoolean(whichEvent: Int, kw: String, name: String, checked: Boolean) {
        super.addBooleanView(name, checked).apply {
            if (isSokobanSettings) {
                generatorOfRow(name).takeIf { it > 0 }?.let { generatorRows += it to this }
            }
            if (backend === SOLO && name.startsWith("Jigsaw")) {
                jigsawHack(this)
                setOnClickListener { jigsawHack(this) }
            }
        }
    }

    private fun jigsawHack(jigsawCheckbox: CheckBox) {
        val colTV = table.findViewWithTag<View>(COLS_EDITTEXT_TAG)
        val rowTV = table.findViewWithTag<View>(ROWS_EDITTEXT_TAG)
        if (colTV is TextView && rowTV is TextView) {
            val cols = colTV.intValue ?: 3
            val rows = rowTV.intValue ?: 3
            if (jigsawCheckbox.isChecked) {
                colTV.intValue = cols * rows
                rowTV.intValue = 1
                rowTV.isEnabled = false
                table.findViewWithTag<TextView>(COLS_LABEL_TAG).setText(R.string.Size_of_sub_blocks)
                table.findViewWithTag<View>(ROWS_ROW_TAG).visibility = View.INVISIBLE
            } else {
                if (rows == 1) {
                    val size = sqrt(cols.toDouble()).toInt().coerceAtLeast(2)
                    colTV.intValue = size
                    rowTV.intValue = size
                }
                rowTV.isEnabled = true
                table.findViewWithTag<TextView>(COLS_LABEL_TAG).setText(R.string.Columns_of_sub_blocks)
                table.findViewWithTag<View>(ROWS_ROW_TAG).visibility = View.VISIBLE
            }
        }
    }

    @UsedByJNI
    override fun dialogShow() {
        showSelectedGeneratorRows()
        dialogBuilder.create().show()
    }

    companion object {
        private const val COLUMNS_OF_SUB_BLOCKS = "Columns of sub-blocks"
        private const val ROWS_OF_SUB_BLOCKS = "Rows of sub-blocks"
        private const val COLS_EDITTEXT_TAG = "colsEditText"
        private const val ROWS_EDITTEXT_TAG = "rowsEditText"
        private const val COLS_LABEL_TAG = "colLabel"
        private const val ROWS_ROW_TAG = "rowsRow"
        private const val SOKOBAN_GENERATOR_CHOICE = "Level generator"
        // the name in front of the colon in the row names, for generators 1, 2, 3 and 4
        private val SOKOBAN_GENERATOR_ROW_PREFIXES = listOf("Boulders", "MCTS", "YASGen", "Rooms")
    }
}