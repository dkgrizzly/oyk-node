// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.content.Intent
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class OykAndroidUnitTest {

    @Test
    fun internalConfigSplitThemesAndCloning() {
        val ctx = ApplicationProvider.getApplicationContext<android.app.Application>()
        val repo = OykConfigRepository(ctx)

        val cloneName = BuiltinThemes.makeCustomCloneName(emptyList(), isDarkBase = true)
        assertEquals("custom-dark", cloneName)
        val clone2 = BuiltinThemes.makeCustomCloneName(
            listOf(ThemeConfigState("custom-dark", true, BuiltinThemes.DARK_COLORS)),
            isDarkBase = true
        )
        assertEquals("custom-dark-2", clone2)

        val custom = ThemeConfigState(
            name = cloneName,
            isDarkBase = true,
            colors = BuiltinThemes.DARK_COLORS.toMutableMap().apply {
                put(ThemeRole.ACCENT, "#FF8800")
            }
        )
        val cfg = NodeConfigState(
            callsign = "KR4OYK-7",
            themesFile = "themes.cfg",
            themeName = cloneName,
            interfacesFile = "interfaces.cfg",
            peersFile = "peers.cfg",
            customThemes = listOf(custom)
        )
        val savedNode = repo.saveInternal(cfg)
        assertTrue(savedNode.exists())
        val themesFile = File(savedNode.parentFile, "themes.cfg")
        assertTrue(themesFile.exists())
        val themesTxt = themesFile.readText()
        assertTrue(themesTxt.contains("[theme custom-dark]"))
        assertTrue(themesTxt.contains("accent = #FF8800"))
        assertFalse(themesTxt.contains("[theme dark]"))
        assertFalse(themesTxt.contains("[theme light]"))
        assertFalse(themesTxt.contains("[theme native]"))

        val reloaded = repo.parseConfigBundle(savedNode)
        assertEquals("custom-dark", reloaded.themeName)
        assertEquals("themes.cfg", reloaded.themesFile)
        assertEquals(1, reloaded.customThemes.size)
        assertEquals("#FF8800", reloaded.customThemes[0].colors[ThemeRole.ACCENT])
    }

    @Test
    fun sdImportExportAndLazyPermissionDeclinedState() {
        val ctx = ApplicationProvider.getApplicationContext<android.app.Application>()
        val repo = OykConfigRepository(ctx)
        assertFalse(repo.isOnboardingShown)

        repo.markStoragePermissionResult(false)
        assertEquals(StoragePermissionState.DECLINED, repo.storagePermissionState())

        val exportDir = File(ctx.cacheDir, "sd_sim_export")
        exportDir.deleteRecursively()
        val exported = repo.exportToDirectory(NodeConfigState(callsign = "W4TST-7"), exportDir)
        assertTrue(exported.exists())
        val imported = repo.importFromFile(exported)
        assertEquals("W4TST-7", imported.callsign)
    }

    @Test
    fun dmrIdValidationAndIpConversion() {
        assertTrue(PeerValidator.isValidHostOrDmrId("3112138"))
        assertEquals("44.47.124.202", PeerValidator.dmrIdToIpv4(3112138L))
        assertFalse(PeerValidator.isValidHostOrDmrId("0"))
        assertFalse(PeerValidator.isValidHostOrDmrId("16777216"))
        assertTrue(PeerValidator.isValidHostOrDmrId("12.34.56.78"))
    }

    @Test
    fun silentGracefulShutdownOnTaskRemovedAndDeviceShutdown() {
        val ctx = ApplicationProvider.getApplicationContext<android.app.Application>()
        val baseCount = OykNodeService.shutdownCount

        val svc = Robolectric.buildService(OykNodeService::class.java).create().get()
        OykNodeService.running = true
        svc.onTaskRemoved(Intent(Intent.ACTION_MAIN))
        assertFalse(OykNodeService.running)
        assertEquals(baseCount + 1, OykNodeService.shutdownCount)

        OykNodeService.running = true
        OykShutdownReceiver().onReceive(ctx, Intent(Intent.ACTION_SHUTDOWN))
        assertFalse(OykNodeService.running)
        assertEquals(baseCount + 2, OykNodeService.shutdownCount)

        OykNodeService.running = true
        OykShutdownReceiver().onReceive(ctx, Intent(Intent.ACTION_REBOOT))
        assertFalse(OykNodeService.running)
        assertEquals(baseCount + 3, OykNodeService.shutdownCount)
    }

    @Test
    fun playStoreFlavorManifestHasNoManageExternalStorage() {
        if (!BuildConfig.IS_SIDELOAD) {
            val ctx = ApplicationProvider.getApplicationContext<android.app.Application>()
            val pkgInfo = ctx.packageManager.getPackageInfo(
                ctx.packageName,
                android.content.pm.PackageManager.GET_PERMISSIONS
            )
            val perms = pkgInfo.requestedPermissions?.toList().orEmpty()
            assertFalse(
                "playStore flavor MUST NOT declare MANAGE_EXTERNAL_STORAGE",
                perms.contains("android.permission.MANAGE_EXTERNAL_STORAGE")
            )
        }
    }

    @Test
    fun jailedFileBrowserBlocksEscapesAndConditionalParent() {
        val ctx = ApplicationProvider.getApplicationContext<android.app.Application>()
        val base = File(ctx.cacheDir, "fm_jail_test").apply { deleteRecursively(); mkdirs() }
        val inc = File(base, "incoming").apply { mkdirs() }
        val files = File(base, "files").apply { mkdirs() }
        val sub = File(inc, "W3TRM-1").apply { mkdirs() }
        File(sub, "fieldday.txt").writeText("CQ Field Day\n")

        val rootList = OykJailedFileBrowser.listDir(inc, "")!!
        assertEquals("", rootList.first)
        assertFalse(rootList.second.any { it.name == ".." || it.isParent })
        assertTrue(OykJailedFileBrowser.enterSubdir(inc, "", "..").isFailure)
        assertNull(OykJailedFileBrowser.listDir(inc, "../../etc"))

        val nextRel = OykJailedFileBrowser.enterSubdir(inc, "", "W3TRM-1").getOrThrow()
        assertEquals("W3TRM-1", nextRel)
        val subList = OykJailedFileBrowser.listDir(inc, nextRel)!!
        assertTrue(subList.second.first().let { it.name == ".." && it.isParent })
        assertEquals("", OykJailedFileBrowser.enterSubdir(inc, nextRel, "..").getOrThrow())

        val dest = OykJailedFileBrowser.approveRelFile(inc, "W3TRM-1/fieldday.txt", files, "").getOrThrow()
        assertTrue(dest.exists())
        assertEquals("files", dest.parentFile.name)
    }
}
