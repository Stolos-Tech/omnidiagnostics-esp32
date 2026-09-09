package one.espos.app

/**
 * AdminViewModel — стан для Server-екрана (task 3/4), Бібліотеки мереж/пристроїв
 * (task 5) та експорту WiFi телефона (task 7). Незалежний від MainViewModel, щоб
 * середовища «сервер» і «плата» були розділені (task 4).
 */
import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.*
import one.espos.client.AdminClient

class AdminViewModel(app: Application) : AndroidViewModel(app) {
    // з'єднання з сервером (dashboard база + gateway-токен зберігаються у Prefs)
    var serverBase by mutableStateOf("100.80.30.64:8080")
    var token by mutableStateOf("")

    var health by mutableStateOf<JsonObject?>(null)
    var services by mutableStateOf<Map<String, String>>(emptyMap())
    var boards by mutableStateOf<List<JsonObject>>(emptyList())
    var msg by mutableStateOf("")
    var busy by mutableStateOf(false)
    var uk by mutableStateOf(false)                       // мова (синк з MainViewModel.lang)
    private fun tr(u: String, e: String) = if (uk) u else e

    var networks by mutableStateOf<List<JsonObject>>(emptyList())
    var devices by mutableStateOf<List<JsonObject>>(emptyList())
    var openProfile by mutableStateOf<JsonObject?>(null)
    var profileTitle by mutableStateOf("")
    // Devintel-розвідка відкритого пристрою (порти/ОС/NetBIOS) + скан на вимогу.
    var deviceIntel by mutableStateOf<JsonObject?>(null)
    var intelBusy by mutableStateOf(false)
    private var curMac = ""; private var curIp = ""

    private fun io(block: suspend () -> Unit) = viewModelScope.launch {
        withContext(Dispatchers.IO) { runCatching { block() }.onFailure { msg = "err: ${it.message}" } }
    }

    // ---- server (task 3) ----
    fun refreshServer() = io {
        health = AdminClient.health(serverBase, token)
        AdminClient.services(serverBase, token)?.let { s ->
            services = s.mapValues { it.value.jsonPrimitive.content }
        }
        AdminClient.boards(serverBase, token)?.let { arr ->
            boards = arr.mapNotNull { it as? JsonObject }
        }
    }

    fun svc(name: String, action: String) = io {
        msg = tr("$action $name…", "$action $name…"); AdminClient.serviceAction(serverBase, token, name, action); refreshServerSync()
        msg = "$action $name ok"
    }

    fun rebootServer(mode: String = "reboot") = io {
        busy = true; AdminClient.serverReboot(serverBase, token, mode); msg = tr("сервер: $mode надіслано", "server $mode sent"); busy = false
    }

    fun rebootBoard(id: Int) = io { msg = tr("рестарт плати #$id…", "reboot board #$id…"); AdminClient.boardReboot(serverBase, token, id); refreshServerSync() }
    fun discoverBoards() = io { AdminClient.boardsDiscover(serverBase, token); refreshServerSync() }

    private suspend fun refreshServerSync() {
        AdminClient.services(serverBase, token)?.let { s -> services = s.mapValues { it.value.jsonPrimitive.content } }
        AdminClient.boards(serverBase, token)?.let { arr -> boards = arr.mapNotNull { it as? JsonObject } }
    }

    // ---- library (task 5) ----
    fun refreshLibrary() = io {
        networks = AdminClient.networks(serverBase, token)?.mapNotNull { it as? JsonObject } ?: emptyList()
        val now = System.currentTimeMillis() / 1000
        fun lastSeen(o: JsonObject) = o["last_seen"]?.jsonPrimitive?.longOrNull ?: 0L
        // Онлайн (бачено <15хв тому) — зверху; далі за свіжістю last_seen.
        devices = (AdminClient.devices(serverBase, token)?.mapNotNull { it as? JsonObject } ?: emptyList())
            .sortedWith(compareByDescending<JsonObject> { (now - lastSeen(it)) < 900 }
                .thenByDescending { lastSeen(it) })
    }

    // ---- мережа: часова стрічка (netlog) + допуск (netguard) ----
    var netEvents by mutableStateOf<List<JsonObject>>(emptyList())
    var netState by mutableStateOf<List<JsonObject>>(emptyList())
    var admPending by mutableStateOf<List<JsonObject>>(emptyList())
    var admApproved by mutableStateOf<List<JsonObject>>(emptyList())
    var admDenied by mutableStateOf<List<JsonObject>>(emptyList())
    fun refreshNetGuard() = io {
        netEvents = one.espos.client.Updater.netlogEvents(serverBase)?.mapNotNull { it as? JsonObject } ?: emptyList()
        netState = one.espos.client.Updater.netlogState(serverBase)?.mapNotNull { it as? JsonObject } ?: emptyList()
        admPending = one.espos.client.Updater.netguardList(serverBase, "pending")?.mapNotNull { it as? JsonObject } ?: emptyList()
        admApproved = one.espos.client.Updater.netguardList(serverBase, "approved")?.mapNotNull { it as? JsonObject } ?: emptyList()
        admDenied = one.espos.client.Updater.netguardList(serverBase, "denied")?.mapNotNull { it as? JsonObject } ?: emptyList()
    }
    fun admit(mac: String, status: String) = io {
        msg = "$status $mac…"; one.espos.client.Updater.netguardSet(serverBase, mac, status)
        refreshNetGuardSync()
    }
    private suspend fun refreshNetGuardSync() {
        admPending = one.espos.client.Updater.netguardList(serverBase, "pending")?.mapNotNull { it as? JsonObject } ?: emptyList()
        admApproved = one.espos.client.Updater.netguardList(serverBase, "approved")?.mapNotNull { it as? JsonObject } ?: emptyList()
        admDenied = one.espos.client.Updater.netguardList(serverBase, "denied")?.mapNotNull { it as? JsonObject } ?: emptyList()
    }

    fun openDevice(id: Int, name: String) = io {
        profileTitle = name; deviceIntel = null; curMac = ""; curIp = ""
        val prof = AdminClient.device(serverBase, token, id); openProfile = prof
        val dev = prof?.get("device") as? JsonObject
        curMac = (dev?.get("mac") as? JsonPrimitive)?.contentOrNull ?: ""
        curIp = (dev?.get("ip") as? JsonPrimitive)?.contentOrNull ?: ""
        if (curMac.isNotBlank()) deviceIntel = one.espos.client.Updater.deviceIntel(serverBase, curMac)
    }
    fun openNetwork(id: Int, name: String) = io { profileTitle = name; deviceIntel = null; openProfile = AdminClient.network(serverBase, token, id) }
    fun closeProfile() { openProfile = null; deviceIntel = null }
    /** Глибокий скан відкритого пристрою ЗАРАЗ -> оновити intel. */
    fun scanDeviceNow() = io {
        if (curMac.isBlank()) return@io
        intelBusy = true; msg = tr("скан $curMac…", "scan $curMac…")
        one.espos.client.Updater.deviceScan(serverBase, curMac, curIp)
        deviceIntel = one.espos.client.Updater.deviceIntel(serverBase, curMac)
        intelBusy = false; msg = tr("скан завершено", "scan done")
    }
    fun makeTarget(kind: String, id: Int, label: String) = io { AdminClient.setTarget(serverBase, token, kind, id, label); msg = tr("таргет: $label", "target set: $label") }
    fun trust(id: Int, t: String) = io { AdminClient.setTrust(serverBase, token, id, t); refreshLibrary() }

    // ---- phone wifi export (task 7) ----
    fun exportPhoneWifi(nets: List<Triple<String, String, String>>) = io {
        val arr = buildJsonArray {
            nets.forEach { (ssid, psk, enc) ->
                add(buildJsonObject { put("ssid", ssid); put("psk", psk); put("encryption", enc) })
            }
        }
        val body = buildJsonObject { put("device", "android"); put("networks", arr) }.toString()
        val r = AdminClient.importPhoneWifi(serverBase, token, body)
        msg = tr("експортовано ${r?.get("imported")?.jsonPrimitive?.content ?: "?"} мереж", "exported ${r?.get("imported")?.jsonPrimitive?.content ?: "?"} networks")
    }
}
