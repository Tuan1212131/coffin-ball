plugins { id("com.android.application") }
android {
    namespace = "com.re.coffinball"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.re.coffinball"
        minSdk = 24
        targetSdk = 28
        versionCode = 1
        versionName = "1.0"
    }
    buildTypes { release { isMinifyEnabled = false } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
}
