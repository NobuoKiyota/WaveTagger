import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// local.properties (not in Git): sdk.dir, and this PC's / this user's values (the Drive folder, the shared signing key)
val local = Properties().apply { rootProject.file("local.properties").takeIf { it.exists() }?.inputStream()?.use { load(it) } }

android {
    namespace = "jp.wavetagger.rec"
    compileSdk = 34

    defaultConfig {
        applicationId = "jp.wavetagger.rec"
        minSdk = 29
        targetSdk = 34
        versionCode = 1
        versionName = "0.1-m1"
        // The Drive folder the recordings go to, set once in local.properties (not in Git): defaultFolder=<folder ID>. It only fills
        // the setting the first time, so the app never starts with an empty folder after a reinstall.
        buildConfigField("String", "DEFAULT_FOLDER", "\"" + (local.getProperty("defaultFolder") ?: "") + "\"")
    }
    buildFeatures { buildConfig = true }

    // One signing key for every PC: the Google sign-in (Android OAuth client) is tied to the key's SHA-1, and the phone only takes an
    // update signed with the key it was installed with. Each PC's own debug key differs, so local.properties can point at a shared
    // keystore (kept in the Drive config folder, not in Git): signingStore=<path>, and optionally signingStorePassword /
    // signingKeyAlias / signingKeyPassword (the Android debug key's defaults: android / androiddebugkey / android).
    val sharedStore = local.getProperty("signingStore")?.let { file(it) }
    if (sharedStore != null) {
        if (!sharedStore.exists()) throw GradleException("signingStore in local.properties does not exist: $sharedStore")
        signingConfigs.create("shared") {
            storeFile = sharedStore
            storePassword = local.getProperty("signingStorePassword") ?: "android"
            keyAlias = local.getProperty("signingKeyAlias") ?: "androiddebugkey"
            keyPassword = local.getProperty("signingKeyPassword") ?: "android"
        }
        buildTypes.getByName("debug").signingConfig = signingConfigs.getByName("shared")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

// The screens are the ones of the browser version: docs/ is the only copy. They are copied into the app's assets at build time.
val webOut = layout.buildDirectory.dir("generated/web")
val syncWeb by tasks.registering(Copy::class) {
    from(rootProject.file("../../docs")) {
        include("index.html", "app.js", "wav.js", "loudness.js", "scope.js", "native.js")
        into("web")
    }
    into(webOut)
}
android.sourceSets.getByName("main").assets.srcDir(webOut)
tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }.configureEach { dependsOn(syncWeb) }

dependencies {
    implementation("androidx.webkit:webkit:1.12.1")
    implementation("androidx.work:work-runtime:2.9.1")           // sending that survives the app being closed, retries by itself
    implementation("com.google.android.gms:play-services-auth:21.2.0") // AuthorizationClient: the Google consent and the access token
    testImplementation("junit:junit:4.13.2")
    testImplementation("com.squareup.okhttp3:mockwebserver:4.12.0") // a fake Drive for the upload tests
    testImplementation("org.json:json:20231013") // the android.jar one is only a stub in plain JVM tests
}
