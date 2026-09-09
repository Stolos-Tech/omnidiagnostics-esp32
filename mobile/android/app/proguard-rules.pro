# R8/ProGuard — обфускація release. Бібліотеки (serialization/okhttp/compose) везуть власні
# consumer-правила; тут — підстраховка для нашого коду й рефлексивних місць.

-keepattributes *Annotation*, InnerClasses, Signature, EnclosingMethod

# ── kotlinx.serialization: зберегти @Serializable класи + їхні згенеровані серіалізатори ──
-keepclassmembers class one.espos.** {
    *** Companion;
}
-keepclasseswithmembers class one.espos.** {
    kotlinx.serialization.KSerializer serializer(...);
}
-keep,includedescriptorclasses class one.espos.**$$serializer { *; }
-keep @kotlinx.serialization.Serializable class one.espos.** { *; }
-keepclassmembers enum * { *; }
-dontnote kotlinx.serialization.**

# ── USB-serial (рефлексивне зондування драйверів) ──
-keep class com.hoho.android.usbserial.** { *; }

# ── OkHttp/Okio ──
-dontwarn okhttp3.**
-dontwarn okio.**
-dontwarn org.conscrypt.**
-dontwarn org.bouncycastle.**
-dontwarn org.openjsse.**

# ── Kotlin метадані (потрібні serialization/reflection) ──
-keep class kotlin.Metadata { *; }
-keepclassmembers class **$WhenMappings { <fields>; }
