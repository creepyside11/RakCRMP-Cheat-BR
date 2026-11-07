.class public final Lrakcheat/Loader;
.super Ljava/lang/Object;

# Loads librakcheat.so. Any failure is swallowed on purpose: the host
# <clinit> must not throw, otherwise the game's own JNI class dies with it.
.method public static load()V
    .locals 1

    :try_start
    const-string v0, "rakcheat"

    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
    :try_end
    .catch Ljava/lang/Throwable; {:try_start .. :try_end} :catch_all

    return-void

    :catch_all
    return-void
.end method
