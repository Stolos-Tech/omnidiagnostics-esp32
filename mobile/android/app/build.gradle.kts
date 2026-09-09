plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("org.jetbrains.kotlin.plugin.serialization")
}

android {
    namespace = "one.espos.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "one.espos.app"
        minSdk = 26
        targetSdk = 34
        versionCode = 70
        versionName = "0.70"
    }
    signingConfigs {
        create("relsign") {   // підпис release debug-keystore -> APK встановлюється (раніше був unsigned!)
            storeFile = file(System.getProperty("user.home") + "/.android/debug.keystore")
            storePassword = "android"; keyAlias = "androiddebugkey"; keyPassword = "android"
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false       // R8 ВІДКОЧЕНО: обфускація ламала Compose-екрани (Сміт зникав)
            signingConfig = signingConfigs.getByName("relsign")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.09.02")
    implementation(composeBom)
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.core:core-ktx:1.13.1")   // FileProvider для встановлення APK
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.6")

    // Keystore-шифрування креденшелів (HMAC-seed) у стані спокою
    implementation("androidx.security:security-crypto:1.1.0-alpha06")
    // Ed25519-верифікація OTA-manifest (та сама версія Tink, що тягне security-crypto → без конфлікту)
    implementation("com.google.crypto.tink:tink-android:1.8.0")

    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")

    // USB-OTG послідовний доступ до плати (CP210x / CH34x / CH9102 / FTDI) — флеш прошивки з телефона
    implementation("com.github.mik3y:usb-serial-for-android:3.8.1")
}
