// Helpers for front ends of Pd's "iemgui" objects (bng, tgl, sliders...).
.pragma library

// Pd's default colours; mapped to the theme so patches look right in dark mode.
const defaultBg = ["#fcfcfc", "#ffffff"]
const defaultFg = ["#000000"]

// Pd stores colours either as "#rrggbb" symbols (0.51+) or as old packed
// negative integers. Returns a colour string, or the fallback.
function color(arg, fallback, defaults) {
    if (arg === undefined || arg === null)
        return fallback
    if (typeof arg === "string") {
        if (arg.length === 7 && arg[0] === "#")
            return (defaults || []).indexOf(arg.toLowerCase()) >= 0 ? fallback : arg
        return fallback
    }
    if (typeof arg === "number" && arg < 0) {
        // Old format: -1 - (r6 << 12 | g6 << 6 | b6)
        const v = -1 - arg
        const r = ((v >> 12) & 63) << 2, g = ((v >> 6) & 63) << 2, b = (v & 63) << 2
        return Qt.rgba(r / 255, g / 255, b / 255, 1)
    }
    return fallback
}

function bg(arg, theme) { return color(arg, theme.boxFill, defaultBg) }
function fg(arg, theme) { return color(arg, theme.text, defaultFg) }

// "empty" and "-" mean "no label".
function label(arg) {
    if (typeof arg !== "string" || arg === "empty" || arg === "-")
        return ""
    return arg.replace(/\\ /g, " ")
}

function num(arg, fallback) { return typeof arg === "number" ? arg : fallback }
