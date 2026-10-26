package jp.wavetagger.rec

import android.app.PendingIntent
import android.content.Context
import com.google.android.gms.auth.api.identity.AuthorizationClient
import com.google.android.gms.auth.api.identity.AuthorizationRequest
import com.google.android.gms.auth.api.identity.Identity
import com.google.android.gms.common.api.Scope
import com.google.android.gms.tasks.Tasks
import java.util.concurrent.TimeUnit

/**
 * Google's consent and access token (Google Play services' AuthorizationClient). The first time (and after the consent runs out or
 * is withdrawn) the user sees Google's own consent screen; after that the token is renewed without any screen, also from the background.
 * Scope `drive`: the Inbox folder already exists, which `drive.file` could not write into.
 */
object GoogleAuth {
    private const val DRIVE = "https://www.googleapis.com/auth/drive"

    sealed class Outcome {
        class Token(val value: String) : Outcome()
        class NeedsConsent(val pending: PendingIntent) : Outcome()
        class Failed(val message: String) : Outcome()
    }

    fun request(): AuthorizationRequest = AuthorizationRequest.builder().setRequestedScopes(listOf(Scope(DRIVE))).build()

    fun client(context: Context): AuthorizationClient = Identity.getAuthorizationClient(context)

    /** For the background: blocks until Google answers. Never shows anything. */
    fun silent(context: Context): Outcome = try {
        val r = Tasks.await(client(context).authorize(request()), 30, TimeUnit.SECONDS)
        val pending = r.pendingIntent
        val token = r.accessToken
        when {
            r.hasResolution() && pending != null -> Outcome.NeedsConsent(pending)
            token != null -> Outcome.Token(token)
            else -> Outcome.Failed("Google がトークンを返しませんでした")
        }
    } catch (e: Throwable) {
        Outcome.Failed(e.message ?: e.toString())
    }
}
