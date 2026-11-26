// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

fun parseComposeColor(hex: String, fallback: Color): Color {
    val clean = hex.trim().removePrefix("#")
    if (clean.length != 6) return fallback
    return try {
        Color(clean.toLong(16) or 0xFF000000L)
    } catch (_: Throwable) {
        fallback
    }
}

fun resolveEffectiveTheme(
    cfg: NodeConfigState,
    systemDark: Boolean
): ThemeConfigState {
    val name = cfg.themeName.trim()
    return when {
        name.isEmpty() || name.equals("native", true) || name.equals("auto", true) ->
            BuiltinThemes.builtinPalette("native", systemDark)
        name.equals("dark", true) ->
            BuiltinThemes.builtinPalette("dark", true)
        name.equals("light", true) ->
            BuiltinThemes.builtinPalette("light", false)
        else ->
            cfg.customThemes.firstOrNull { it.name.equals(name, true) }
                ?: BuiltinThemes.builtinPalette("native", systemDark)
    }
}

@Composable
fun OykAndroidTheme(
    cfg: NodeConfigState,
    content: @Composable (ThemeConfigState) -> Unit
) {
    val sysDark = isSystemInDarkTheme()
    val effective = resolveEffectiveTheme(cfg, sysDark)
    val context = LocalContext.current

    val colorScheme: ColorScheme = if (effective.name.equals("native", true)) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (sysDark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        } else {
            if (sysDark) darkColorScheme() else lightColorScheme()
        }
    } else {
        val bg = parseComposeColor(effective.colors[ThemeRole.BG] ?: "#10141A", Color(0xFF10141A))
        val fg = parseComposeColor(effective.colors[ThemeRole.FG] ?: "#E2E8F0", Color(0xFFE2E8F0))
        val panelBg = parseComposeColor(effective.colors[ThemeRole.PANEL_BG] ?: "#18202A", Color(0xFF18202A))
        val panelFg = parseComposeColor(effective.colors[ThemeRole.PANEL_FG] ?: "#E2E8F0", Color(0xFFE2E8F0))
        val accent = parseComposeColor(effective.colors[ThemeRole.ACCENT] ?: "#38BDF8", Color(0xFF38BDF8))
        val hdrBg = parseComposeColor(effective.colors[ThemeRole.HEADER_BG] ?: "#1E3A5F", Color(0xFF1E3A5F))
        val hdrFg = parseComposeColor(effective.colors[ThemeRole.HEADER_FG] ?: "#FFFFFF", Color.White)
        val err = parseComposeColor(effective.colors[ThemeRole.ERROR] ?: "#F87171", Color(0xFFF87171))
        val border = parseComposeColor(effective.colors[ThemeRole.BORDER] ?: "#38BDF8", accent)

        if (effective.isDarkBase) {
            darkColorScheme(
                primary = accent,
                onPrimary = hdrFg,
                primaryContainer = hdrBg,
                onPrimaryContainer = hdrFg,
                background = bg,
                onBackground = fg,
                surface = panelBg,
                onSurface = panelFg,
                surfaceVariant = bg,
                onSurfaceVariant = fg,
                outline = border,
                error = err
            )
        } else {
            lightColorScheme(
                primary = accent,
                onPrimary = hdrFg,
                primaryContainer = hdrBg,
                onPrimaryContainer = hdrFg,
                background = bg,
                onBackground = fg,
                surface = panelBg,
                onSurface = panelFg,
                surfaceVariant = bg,
                onSurfaceVariant = fg,
                outline = border,
                error = err
            )
        }
    }

    MaterialTheme(colorScheme = colorScheme) {
        content(effective)
    }
}
