# 14：DDS Pub/Sub

使用内置 `dds` provider 演示 Cyclone DDS 的发布订阅。订阅端是普通 service，
方法名即 topic；发布端是普通 proxy，用单向事件调用发送。

```powershell
.\example_14_dds_pubsub.exe
```

绑定 URI 为 `dds://<domain>`，host 是 DDS domain；端口可省略（旧写法
`dds://<domain>:1` 仍然兼容，provider 会忽略端口）。示例启用 `intra_process = "only"`：事件不经过
DDS 序列化和网络，发布端把消息指针直接投递给同 provider 的本地订阅者。

topic 由完整路由派生为 `<Service>_<method>_evt`（非字母数字字符替换为 `_`），
因此不同 service 的同名方法不会互相串流；本地投递使用同一个 route 和 payload 指针。

程序会重复发布直到订阅端收到，避免 DDS discovery 完成前丢首包。
