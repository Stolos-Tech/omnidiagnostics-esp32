package one.espos.app

/**
 * Prefs — параметри під'єднання (host/pin, gateway host/token, режим, updateUrl) у звичайних
 * SharedPreferences («espos»). HMAC-seed (динамічний креденшел) — ОКРЕМО у Keystore-шифрованому
 * сховищі («espos_secure», AES256-GCM через Android Keystore), щоб не лежав plaintext.
 *
 * Безпека без ризику зв'язку: host/pin/token лишаються у звичайному сторі недоторканими — навіть
 * якщо шифросховище недоступне (старий девайс, збій Keystore), зв'язок не втрачається; seed тоді
 * просто null → PIN-фолбек знову провізує його (enroll). Увесь доступ до шифросховища у try/catch.
 */
import android.content.Context
import android.content.SharedPreferences
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

object Prefs {
    private fun sp(ctx: Context) = ctx.getSharedPreferences("espos", Context.MODE_PRIVATE)

    /** Keystore-шифрований стор лише для секретів; null, якщо недоступний (фолбек safe). */
    private fun secure(ctx: Context): SharedPreferences? = try {
        val key = MasterKey.Builder(ctx).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build()
        EncryptedSharedPreferences.create(
            ctx, "espos_secure", key,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
        )
    } catch (e: Exception) { null }

    fun load(ctx: Context, vm: MainViewModel) {
        val p = sp(ctx)
        vm.useGateway = p.getBoolean("useGateway", vm.useGateway)
        vm.gwHost = p.getString("gwHost", vm.gwHost) ?: vm.gwHost
        vm.gwToken = p.getString("gwToken", vm.gwToken) ?: vm.gwToken
        vm.host = p.getString("host", vm.host) ?: vm.host
        vm.pin = p.getString("pin", vm.pin) ?: vm.pin
        vm.updateUrl = p.getString("updateUrl", vm.updateUrl) ?: vm.updateUrl
        vm.autoConnect = p.getBoolean("autoConnect", false)

        // seed: спершу з шифросховища; якщо там нема — міграція зі старого plaintext-ключа
        val sec = secure(ctx)
        var seed = try { sec?.getString("hmacSeed", null) } catch (e: Exception) { null }
        if (seed == null) {
            val legacy = p.getString("hmacSeed", null)
            if (!legacy.isNullOrBlank()) {
                seed = legacy
                try { sec?.edit()?.putString("hmacSeed", legacy)?.apply() } catch (e: Exception) {}
                p.edit().remove("hmacSeed").apply()   // прибрати plaintext-копію
            }
        }
        vm.hmacSeed = seed
    }

    fun save(ctx: Context, vm: MainViewModel) {
        sp(ctx).edit()
            .putBoolean("useGateway", vm.useGateway)
            .putString("gwHost", vm.gwHost)
            .putString("gwToken", vm.gwToken)
            .putString("host", vm.host)
            .putString("pin", vm.pin)
            .putString("updateUrl", vm.updateUrl)
            .putBoolean("autoConnect", vm.autoConnect)
            .remove("hmacSeed")                        // seed більше НЕ у plaintext-сторі
            .apply()
        try { secure(ctx)?.edit()?.putString("hmacSeed", vm.hmacSeed)?.apply() } catch (e: Exception) {}
    }
}
