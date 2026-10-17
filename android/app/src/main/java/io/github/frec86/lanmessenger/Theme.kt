package io.github.frec86.lanmessenger

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/* Redesign (design/redesign/spec.md, Android = Material 3). Scheme = M3 SchemeTonalSpot from seed #005FB8,
 * fixed (no wallpaper dynamic colour, so every device looks like the approved mockup). Chat-specific roles
 * (bubbles, wallpaper, service pills, online) are extra tokens next to the scheme. */
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
        secondaryContainer = secCont, onSecondaryContainer = onSecCont,
        background = window, onBackground = fg, surface = window, onSurface = fg, onSurfaceVariant = fg2, surfaceVariant = chip,
        surfaceTint = Color.Transparent, surfaceContainerLowest = theirs, surfaceContainerLow = navBar, surfaceContainer = navBar,
        surfaceContainerHigh = dialog, surfaceContainerHighest = chip, outline = if (dark) c(0x8E9099) else c(0x74777F), outlineVariant = divider,
        error = error, scrim = Color.Black)
    companion object {
        val Light = Pal(window = c(0xF9F9FF), chat = c(0xE7E8EE), rowSel = c(0xD9E3F8), fg = c(0x191C20), fg2 = c(0x43474E), accent = c(0x3F5F90), onAccent = c(0xFFFFFF),
            mine = c(0xD6E3FF), mineFg = c(0x191C20), mineMeta = c(0x3E4758), theirs = c(0xFFFFFF), theirsFg = c(0x191C20), meta = c(0x43474E),
            divider = c(0xC4C6CF), badge = c(0x3F5F90), badgeFg = c(0xFFFFFF), online = c(0x1B7F2B), field = c(0xFFFFFF), chip = c(0xE7E8EE), chipFg = c(0x43474E),
            service = c(0xE2E2E9), serviceFg = c(0x43474E), read = c(0x3F5F90), wall = c(0x191C20), wallAlpha = .04f, tile = c(0xD6E3FF), tileFg = c(0x264777),
            secCont = c(0xD9E3F8), onSecCont = c(0x3E4758), pCont = c(0xD6E3FF), onPCont = c(0x264777), bar = c(0xF9F9FF), navBar = c(0xEDEDF4),
            dialog = c(0xEDEDF4), error = c(0xBA1A1A), warning = c(0x8A5100), imageScrim = Color(0x8C000000))
        val Dark = Pal(window = c(0x111318), chat = c(0x111318), rowSel = c(0x3E4758), fg = c(0xE2E2E9), fg2 = c(0xC4C6CF), accent = c(0xA8C8FF), onAccent = c(0x07305F),
            mine = c(0x264777), mineFg = c(0xE2E2E9), mineMeta = c(0xC4D3EE), theirs = c(0x282A2F), theirsFg = c(0xE2E2E9), meta = c(0xC4C6CF),
            divider = c(0x43474E), badge = c(0xA8C8FF), badgeFg = c(0x07305F), online = c(0x7DDB86), field = c(0x282A2F), chip = c(0x1D2024), chipFg = c(0xC4C6CF),
            service = c(0x282A2F), serviceFg = c(0xC4C6CF), read = c(0xA8C8FF), wall = c(0xE2E2E9), wallAlpha = .035f, tile = c(0x264777), tileFg = c(0xD6E3FF),
            secCont = c(0x3E4758), onSecCont = c(0xD9E3F8), pCont = c(0x264777), onPCont = c(0xD6E3FF), bar = c(0x1D2024), navBar = c(0x1D2024),
            dialog = c(0x282A2F), error = c(0xFFB4AB), warning = c(0xFFB86E), imageScrim = Color(0x8C000000))
    }
}
val Palette = Pal
val LocalPalette = staticCompositionLocalOf { Pal.Light }

/** M3 type scale (Roboto + Noto Sans CJK SC system fallback), the roles the spec uses. */
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
/** Initials: Latin names -> first letters of the first two words ("Alice Chen" -> "AC"); otherwise the first character. */
fun initials(name: String): String {
    val n = name.trim(); if (n.isEmpty()) return "?"
    if (n[0].code < 128 && n[0].isLetter()) { val w = n.split(Regex("\\s+")).filter { it.isNotEmpty() }; return w.take(2).joinToString("") { it.take(1) }.uppercase() }
    val cp = n.codePointAt(0); return String(Character.toChars(cp))
}
