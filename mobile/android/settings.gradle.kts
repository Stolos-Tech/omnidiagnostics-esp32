pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven { url = uri("https://jitpack.io") }   // usb-serial-for-android (USB-OTG флеш плати)
    }
}
rootProject.name = "EspOsApp"
include(":app")
