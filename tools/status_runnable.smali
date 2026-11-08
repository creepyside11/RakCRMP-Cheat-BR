.class public final Lrakcheat/StatusRunnable;
.super Ljava/lang/Object;
.implements Ljava/lang/Runnable;

.field private final message:Ljava/lang/String;

.method public constructor <init>(Ljava/lang/String;)V
    .locals 0
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    iput-object p1, p0, Lrakcheat/StatusRunnable;->message:Ljava/lang/String;
    return-void
.end method

.method public run()V
    .locals 3
    :try_start
    invoke-static {}, Landroid/app/ActivityThread;->currentApplication()Landroid/app/Application;
    move-result-object v0
    if-eqz v0, :done
    iget-object v1, p0, Lrakcheat/StatusRunnable;->message:Ljava/lang/String;
    const/4 v2, 0x1
    invoke-static {v0, v1, v2}, Landroid/widget/Toast;->makeText(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;
    move-result-object v0
    invoke-virtual {v0}, Landroid/widget/Toast;->show()V
    :try_end
    .catch Ljava/lang/Throwable; {:try_start .. :try_end} :done
    :done
    return-void
.end method
