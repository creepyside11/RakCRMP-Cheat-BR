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
    const-string v0, "RakCRMP Cheat: native library load failed"
    invoke-static {v0}, Lrakcheat/Loader;->status(Ljava/lang/String;)V
    return-void
.end method

.method public static status(Ljava/lang/String;)V
    .locals 2

    :try_start_status
    invoke-static {}, Landroid/app/ActivityThread;->currentApplication()Landroid/app/Application;
    move-result-object v0
    if-eqz v0, :done_status

    new-instance v1, Lrakcheat/StatusRunnable;
    invoke-direct {v1, p0}, Lrakcheat/StatusRunnable;-><init>(Ljava/lang/String;)V
    invoke-virtual {v0}, Landroid/content/Context;->getMainExecutor()Ljava/util/concurrent/Executor;
    move-result-object v0
    invoke-interface {v0, v1}, Ljava/util/concurrent/Executor;->execute(Ljava/lang/Runnable;)V
    :try_end_status
    .catch Ljava/lang/Throwable; {:try_start_status .. :try_end_status} :done_status

    :done_status
    return-void
.end method
