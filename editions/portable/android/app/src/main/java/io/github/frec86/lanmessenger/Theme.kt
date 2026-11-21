package io.github.frec86.lanmessenger

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/* Liquid Glass chrome (opaque regular material, not Material You).
 * Fills are the lg_* tokens in tools/palette.json. Material3 stays only as the widget toolkit
 * (dialogs, menus, text); the colour scheme is these tokens, not SchemeTonalSpot. */
private fun c(v: Long) = Color(0xFF000000 or v)

data class Pal(
    val window: Color, val chat: Color, val rowSel: Color, val fg: Color, val fg2: Color, val accent: Color, val onAccent: Color,
    val mine: Color, val mineFg: Color, val mineMeta: Color, val theirs: Color, val theirsFg: Color, val meta: Color,
    val divider: Color, val badge: Color, val badgeFg: Color, val online: Color, val field: Color, val chip: Color, val chipFg: Color,
    val service: Color, val serviceFg: Color, val read: Color, val wall: Color, val wallAlpha: Float, val tile: Color, val tileFg: Color,
    val secCont: Color, val onSecCont: Color, val pCont: Color, val onPCont: Color, val bar: Color, val navBar: Color,
    val dialog: Color, val error: Color, val warning: Color, val imageScrim: Color,
) {
    // legacy names used by dialogs/tests
    val cardBg get() = dialog
    val textPrimary get() = fg
    fun scheme(dark: Boolean): ColorScheme = (if (dark) darkColorScheme() else lightColorScheme()).copy(
        primary = accent, onPrimary = onAccent, primaryContainer = pCont, onPrimaryContainer = onPCont,
        inversePrimary = accent,
        secondary = fg2, onSecondary = window, secondaryContainer = secCont, onSecondaryContainer = onSecCont,
        tertiary = accent, onTertiary = onAccent, tertiaryContainer = pCont, onTertiaryContainer = onPCont,
        background = window, onBackground = fg, surface = dialog, onSurface = fg, onSurfaceVariant = fg2, surfaceVariant = chip,
        surfaceTint = Color.Transparent, surfaceContainerLowest = dialog, surfaceContainerLow = dialog, surfaceContainer = dialog,
        surfaceContainerHigh = dialog, surfaceContainerHighest = dialog, outline = fg2, outlineVariant = divider,
        error = error, onError = if (dark) Color.Black else Color.White, errorContainer = dialog, onErrorContainer = error,
        inverseSurface = fg, inverseOnSurface = window, scrim = Color.Black)
    companion object {
        val Light = Pal(window = c(0xE7EDF6), chat = c(0xE7EDF6), rowSel = c(0xFFFFFF), fg = c(0x1A1A1A), fg2 = c(0x5C5C5C), accent = c(0x005FB8), onAccent = c(0xFFFFFF),
            mine = c(0xD6E6FF), mineFg = c(0x1A1A1A), mineMeta = c(0x5C5C5C), theirs = c(0xFFFFFF), theirsFg = c(0x1A1A1A), meta = c(0x5C5C5C),
            divider = c(0xD5DCE6), badge = c(0x005FB8), badgeFg = c(0xFFFFFF), online = c(0x0F7B0F), field = c(0xFFFFFF), chip = c(0xFFFFFF), chipFg = c(0x5C5C5C),
            service = c(0xD5E0F0), serviceFg = c(0x5C5C5C), read = c(0x005FB8), wall = c(0x1A1A1A), wallAlpha = .04f, tile = c(0xD6E6FF), tileFg = c(0x1A1A1A),
            secCont = c(0xD6E6FF), onSecCont = c(0x1A1A1A), pCont = c(0xD6E6FF), onPCont = c(0x1A1A1A), bar = c(0xFFFFFF), navBar = c(0xD5E0F0),
            dialog = c(0xFFFFFF), error = c(0xC42B1C), warning = c(0x9D5D00), imageScrim = Color(0x8C000000))
        val Dark = Pal(window = c(0x0B1020), chat = c(0x0B1020), rowSel = c(0x243044), fg = c(0xFFFFFF), fg2 = c(0xCCCCCC), accent = c(0x60CDFF), onAccent = c(0x000000),
            mine = c(0x2A4068), mineFg = c(0xFFFFFF), mineMeta = c(0xCCCCCC), theirs = c(0x1C2436), theirsFg = c(0xFFFFFF), meta = c(0xCCCCCC),
            divider = c(0x3A445C), badge = c(0x60CDFF), badgeFg = c(0x000000), online = c(0x6CCB5F), field = c(0x1C2436), chip = c(0x1C2436), chipFg = c(0xCCCCCC),
            service = c(0x1A2740), serviceFg = c(0xCCCCCC), read = c(0x60CDFF), wall = c(0xFFFFFF), wallAlpha = .035f, tile = c(0x2A4068), tileFg = c(0xFFFFFF),
            secCont = c(0x2A4068), onSecCont = c(0xFFFFFF), pCont = c(0x2A4068), onPCont = c(0xFFFFFF), bar = c(0x1C2436), navBar = c(0x1A2740),
            dialog = c(0x1C2436), error = c(0xFF99A4), warning = c(0xFCE100), imageScrim = Color(0x8C000000))
    }
}
val Palette = Pal
val LocalPalette = staticCompositionLocalOf { Pal.Light }

/** Type scale (Roboto + Noto Sans CJK SC). Sizes match the shared ramp; this is not a Material colour system. */
val M3Typography: Typography = run {
    fun t(size: Int, lh: Int, w: FontWeight = FontWeight.Normal, ls: Double = 0.0) = TextStyle(fontSize = size.sp, lineHeight = lh.sp, fontWeight = w, letterSpacing = ls.sp)
    val m = FontWeight.Medium
    Typography(displayLarge = t(57, 64), displayMedium = t(45, 52), displaySmall = t(36, 44),
        headlineLarge = t(32, 40), headlineMedium = t(28, 36), headlineSmall = t(24, 32),
        titleLarge = t(22, 28), titleMedium = t(16, 24, m, .15), titleSmall = t(14, 20, m, .1),
        bodyLarge = t(16, 24, ls = .5), bodyMedium = t(14, 20, ls = .25), bodySmall = t(12, 16, ls = .4),
        labelLarge = t(14, 20, m, .1), labelMedium = t(12, 16, m, .5), labelSmall = t(11, 16, m, .5))
}

@Composable
fun AppTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme(); val p = if (dark) Pal.Dark else Pal.Light
    CompositionLocalProvider(LocalPalette provides p) {
        MaterialTheme(colorScheme = p.scheme(dark), typography = M3Typography, content = content)
    }
}

/* ---------------------------------------------------------------- shared presentation logic (same rules on all 4 platforms) */
/** Avatar colour: FNV-1a 32 over the UTF-8 id (fingerprint or group id), mod 8 (spec §6.1). */
val AVATAR_COLORS = listOf(0x6D4FC2, 0x1F6FB2, 0x0F7A6C, 0xB4471B, 0x2E6B2F, 0x9C2F55, 0x5A5F14, 0x8A4B08).map { c(it.toLong()) }
fun avatarIndex(id: String): Int { var h = 0x811C9DC5.toInt(); for (b in id.toByteArray(Charsets.UTF_8)) { h = h xor (b.toInt() and 0xFF); h *= 16777619 }
    return ((h.toLong() and 0xFFFFFFFFL) % 8).toInt() }
fun avatarColor(id: String) = AVATAR_COLORS[avatarIndex(id)]
/** Initials (same rule on all clients): CJK first -> that one character ("王工" -> "王"); Latin first -> first letters of up to
 *  two Latin words, never mixed with CJK ("Alice Chen" -> "AC", "CI 测试群" -> "C"); otherwise the first character. */
private fun isLatin(c: Char) = c in 'A'..'Z' || c in 'a'..'z'
fun initials(name: String): String {
    val n = name.trim(); if (n.isEmpty()) return "?"
    if (isLatin(n[0])) return n.split(Regex("\\s+")).filter { it.isNotEmpty() && isLatin(it[0]) }.take(2).joinToString("") { it.take(1) }.uppercase()
    val cp = n.codePointAt(0); return String(Character.toChars(cp))
}
