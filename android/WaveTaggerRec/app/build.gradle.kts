plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "jp.wavetagger.rec"
    compileSdk = 34

    defaultConfig {
        applicationId = "jp.wavetagger.rec"
        minSdk = 29
        targetSdk = 34
        versionCode = 1
        versionName = "0.1-m1"
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
