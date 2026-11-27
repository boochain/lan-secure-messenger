import lsm from 'liblsm.so';
import { common } from '@kit.AbilityKit';
import { promptAction } from '@kit.ArkUI';
import { fileIo, fileUri, picker } from '@kit.CoreFileKit';

const UNTRUSTED: string = '对方尚未信任，无法发送图片/文件。请先在联系人菜单中信任对方（或完成 PIN 配对）。';

class ChatRow {
  key: string = '';
  title: string = '';
  sub: string = '';
  unread: number = 0;
  trust: number = 1;
  conv: string = '';
  fp: string = '';
  online: boolean = false;
}

class Msg {
  id: string = '';
  out: boolean = false;
  body: string = '';
  file: string = '';
  path: string = '';
  sender: string = '';
  kind: number = 0;
  status: string = '';
}

class Xfer {
  id: string = '';
  name: string = '';
  status: string = '';
  state: string = '';
  path: string = '';
}

@Entry
@Component
struct Index {
  @State ready: boolean = false;
  @State first: boolean = true;
  @State mode: string = 'device';
  @State tab: number = 0;
  @State rows: ChatRow[] = [];
  @State xfers: Xfer[] = [];
  @State me: string = '';
  @State fp: string = '';
  @State inChat: boolean = false;
  @State conv: string = '';
  @State title: string = '';
  @State trust: number = 1;
  @State peer: string = '';
  @State msgs: Msg[] = [];
  @State draft: string = '';
  @State viewPath: string = '';
  @State sheet: string = '';
  @State field: string = '';
  @State field2: string = '';
  @State relayOn: boolean = false;
  @State relayHost: string = '';
  @State relayPort: string = '9443';
  @State recovery: string = '';
  @State diag: string = '';
  private timer: number = -1;

  aboutToAppear(): void {
    const dir = AppStorage.get<string>('filesDir') ?? '';
    const err = lsm.start(dir, '', '', 0);
    if (err.length > 0) {
      promptAction.showToast({ message: err });
    }
    this.first = !lsm.firstRunDone();
    this.ready = true;
    this.pull();
    this.timer = setInterval(() => { this.pull(); }, 600);
  }

  aboutToDisappear(): void {
    if (this.timer >= 0) {
      clearInterval(this.timer);
    }
  }

  pull(): void {
    lsm.poll();
    const raw = lsm.state();
    if (raw.length < 2) {
      return;
    }
    let root: ESObject;
    try {
      root = JSON.parse(raw) as ESObject;
    } catch (err) {
      return;
    }
    const me = root.me as ESObject;
    this.me = me ? this.esStr(me.name) : '';
    this.fp = me ? this.esStr(me.fp) : '';
    const next: ChatRow[] = [];
    const contacts = this.esArr(root.contacts);
    for (let i = 0; i < contacts.length; i++) {
      const c = contacts[i];
      const row = new ChatRow();
      row.key = this.esStr(c.fp);
      row.fp = row.key;
      row.title = this.esStr(c.name);
      const online = this.esBool(c.online);
      const via = this.esBool(c.via);
      const addr = this.esStr(c.addr);
      row.sub = online ? (via ? '在线 · 经服务器' : '在线') : (addr.length > 0 ? addr : '离线');
      row.unread = this.esNum(c.unread);
      row.trust = this.esNum(c.trust);
      row.online = online;
      row.conv = 'dm:' + row.fp;
      next.push(row);
      if (this.inChat && this.conv === row.conv) {
        this.trust = row.trust;
        this.title = row.title;
      }
    }
    const groups = this.esArr(root.groups);
    for (let i = 0; i < groups.length; i++) {
      const g = groups[i];
      const row = new ChatRow();
      row.key = 'g:' + this.esStr(g.id);
      row.title = this.esStr(g.name);
      row.sub = '群组';
      row.unread = this.esNum(g.unread);
      row.trust = 1;
      row.conv = 'group:' + this.esStr(g.id);
      next.push(row);
    }
    this.rows = next;
    const xs = this.esArr(root.transfers);
    const xout: Xfer[] = [];
    for (let i = 0; i < xs.length; i++) {
      const t = new Xfer();
      t.id = this.esStr(xs[i].id);
      t.name = this.esStr(xs[i].name);
      t.status = this.esStr(xs[i].status);
      t.state = this.esStr(xs[i].state);
      t.path = this.esStr(xs[i].path);
      xout.push(t);
    }
    this.xfers = xout;
    const relay = root.relay as ESObject;
    if (relay) {
      this.relayOn = this.esBool(relay.enabled);
      if (this.sheet !== 'relay') {
        this.relayHost = this.esStr(relay.host);
        const port = this.esNum(relay.port);
        this.relayPort = '' + (port > 0 ? port : 9443);
      }
    }
    if (this.inChat) {
      this.loadMsgs();
    }
  }

  loadMsgs(): void {
    const raw = lsm.messages(this.conv);
    let parsed: ESObject;
    try {
      parsed = JSON.parse(raw) as ESObject;
    } catch (err) {
      return;
    }
    const arr = this.esArr(parsed);
    const out: Msg[] = [];
    for (let i = 0; i < arr.length; i++) {
      const m = new Msg();
      m.id = this.esStr(arr[i].id);
      m.out = this.esBool(arr[i].out);
      m.body = this.esStr(arr[i].body);
      m.file = this.esStr(arr[i].file);
      m.path = this.esStr(arr[i].path);
      m.sender = this.esStr(arr[i].sender);
      m.kind = this.esNum(arr[i].kind);
      m.status = this.esStr(arr[i].status);
      out.push(m);
    }
    this.msgs = out;
  }

  toast(s: string): void {
    if (s.length > 0) {
      promptAction.showToast({ message: s });
    }
  }

  esStr(v: ESObject): string {
    if (typeof v === 'string') {
      return v as string;
    }
    return '';
  }

  esNum(v: ESObject): number {
    if (typeof v === 'number') {
      return v as number;
    }
    return 0;
  }

  esBool(v: ESObject): boolean {
    if (typeof v === 'boolean') {
      return v as boolean;
    }
    return false;
  }

  esArr(v: ESObject): ESObject[] {
    if (Array.isArray(v)) {
      return v as ESObject[];
    }
    return [];
  }

  baseName(name: string): string {
    let n = name;
    const slash = n.lastIndexOf('/');
    if (slash >= 0) {
      n = n.substring(slash + 1);
    }
    if (n.length === 0 || n === '.' || n === '..') {
      return 'file';
    }
    return n;
  }

  openChat(row: ChatRow): void {
    this.conv = row.conv;
    this.title = row.title;
    this.trust = row.trust;
    this.peer = row.fp;
    this.inChat = true;
    this.viewPath = '';
    lsm.markRead(row.conv);
    this.loadMsgs();
  }

  send(): void {
    const text = this.draft;
    if (text.length === 0) {
      return;
    }
    const err = lsm.sendText2(this.conv, text, '');
    this.toast(err);
    if (err.length === 0) {
      this.draft = '';
    }
    this.pull();
  }

  async sendFile(): Promise<void> {
    if (this.trust !== 1 && this.conv.indexOf('dm:') === 0) {
      this.toast(UNTRUSTED);
      return;
    }
    const rawCtx = this.getUIContext().getHostContext();
    if (rawCtx === undefined || rawCtx === null) {
      this.toast('无法打开文件选择');
      return;
    }
    try {
      const view = new picker.DocumentViewPicker(rawCtx as common.UIAbilityContext);
      const opt = new picker.DocumentSelectOptions();
      opt.maxSelectNumber = 1;
      const uris = await view.select(opt);
      if (!uris || uris.length === 0) {
        return;
      }
      const src = uris[0];
      let name = 'file';
      try {
        const fu = new fileUri.FileUri(src);
        if (fu.name.length > 0) {
          name = this.baseName(fu.name);
        }
      } catch (err) {
        name = 'file';
      }
      const dir = (AppStorage.get<string>('filesDir') ?? '') + '/outbox';
      try {
        fileIo.mkdirSync(dir);
      } catch (err) {
        name = name.length > 0 ? name : 'file';
      }
      const dest = dir + '/' + name;
      const srcFile = fileIo.openSync(src, fileIo.OpenMode.READ_ONLY);
      const destFile = fileIo.openSync(dest,
        fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
      try {
        fileIo.copyFileSync(srcFile.fd, destFile.fd);
      } finally {
        fileIo.closeSync(srcFile);
        fileIo.closeSync(destFile);
      }
      this.toast(lsm.sendFile(this.conv, dest));
      this.pull();
    } catch (err) {
      this.toast('没有选到文件');
    }
  }

  isImage(name: string): boolean {
    const n = name.toLowerCase();
    return n.endsWith('.png') || n.endsWith('.jpg') || n.endsWith('.jpeg') || n.endsWith('.webp') || n.endsWith('.gif');
  }

  finishFirst(): void {
    const err = lsm.completeFirstRun('', this.mode, '');
    this.toast(err);
    if (this.mode === 'account') {
      const rk = lsm.accountSetup();
      if (rk.indexOf('RK1-') === 0) {
        this.recovery = rk;
        this.sheet = 'key';
      } else {
        this.toast(rk);
      }
    }
    this.first = !lsm.firstRunDone();
    this.pull();
  }

  build() {
    Column() {
      if (!this.ready) {
        Text('正在打开').fontSize(16).fontColor($r('sys.color.ohos_id_color_text_secondary'))
      } else if (this.first) {
        this.FirstRun()
      } else if (this.viewPath.length > 0) {
        this.Viewer()
      } else if (this.inChat) {
        this.Chat()
      } else {
        this.Home()
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor($r('sys.color.ohos_id_color_sub_background'))
  }

  @Builder FirstRun() {
    Column() {
      Text('选择身份').fontSize(28).fontWeight(FontWeight.Medium)
        .fontColor($r('sys.color.ohos_id_color_text_primary')).margin({ top: 36, bottom: 8 })
      Text('之后可在设置里更改。不使用服务器密码。账号模式把加密身份放在中转上，换机用恢复密钥找回；设备模式只留在本机。')
        .fontSize(14).fontColor($r('sys.color.ohos_id_color_text_secondary')).margin({ bottom: 16 })
      this.Choice('device', '设备模式', '仅本机，局域网直连')
      this.Choice('account', '账号模式', '加密备份在中转上，换机可恢复')
      Button(this.mode === 'account' ? '下一步' : '开始使用')
        .width('100%').height(48).margin({ top: 24 })
        .onClick(() => { this.finishFirst(); })
    }.padding(20).alignItems(HorizontalAlign.Start)
  }

  @Builder Choice(id: string, title: string, sub: string) {
    Column() {
      Text(title).fontSize(16).fontWeight(FontWeight.Medium).fontColor($r('sys.color.ohos_id_color_text_primary'))
      Text(sub).fontSize(13).fontColor($r('sys.color.ohos_id_color_text_secondary')).margin({ top: 4 })
    }
    .width('100%')
    .padding(16)
    .margin({ bottom: 10 })
    .borderRadius(20)
    .backgroundColor($r('sys.color.ohos_id_color_background'))
    .border({ width: this.mode === id ? 2 : 0, color: $r('sys.color.ohos_id_color_emphasize') })
    .onClick(() => { this.mode = id; })
  }

  @Builder Home() {
    Stack() {
      Column() {
        Row() {
          Text('局域网安全通讯').fontSize(20).fontWeight(FontWeight.Medium)
            .fontColor($r('sys.color.ohos_id_color_text_primary'))
          Blank()
          Button('添加').height(32).fontSize(14).onClick(() => { this.field = ''; this.sheet = 'add'; })
        }.width('100%').padding({ left: 16, right: 16, top: 12, bottom: 8 })

        Tabs({ index: this.tab }) {
          TabContent() { this.ChatList() }.tabBar('聊天')
          TabContent() { this.People() }.tabBar('联系人')
          TabContent() { this.Files() }.tabBar('文件')
          TabContent() { this.Settings() }.tabBar('设置')
        }
        .barPosition(BarPosition.End)
        .layoutWeight(1)
        .width('100%')
        .onChange((i: number) => { this.tab = i; })
      }.width('100%').height('100%')

      if (this.sheet.length > 0) {
        Column() {
          this.Sheet()
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor($r('sys.color.ohos_id_color_sub_background'))
      }
    }.width('100%').height('100%')
  }

  @Builder ChatList() {
    List() {
      ForEach(this.rows, (row: ChatRow) => {
        ListItem() {
          Row() {
            Column() {
              Text(row.title.substring(0, 1)).fontSize(18).fontColor($r('sys.color.ohos_id_color_foreground_contrary'))
            }
            .width(44).height(44).borderRadius(22).justifyContent(FlexAlign.Center)
            .backgroundColor($r('sys.color.ohos_id_color_emphasize'))
            Column() {
              Text(row.title).fontSize(16).fontColor($r('sys.color.ohos_id_color_text_primary')).maxLines(1)
              Text(row.sub).fontSize(12).fontColor($r('sys.color.ohos_id_color_text_secondary')).maxLines(1)
            }.alignItems(HorizontalAlign.Start).margin({ left: 12 }).layoutWeight(1)
            if (row.unread > 0) {
              Text('' + row.unread).fontSize(12).fontColor(Color.White)
                .backgroundColor($r('sys.color.ohos_id_color_emphasize'))
                .borderRadius(10).padding({ left: 6, right: 6, top: 2, bottom: 2 })
            }
          }.width('100%').padding(12)
        }.onClick(() => { this.openChat(row); })
      }, (row: ChatRow) => row.key)
    }.layoutWeight(1).width('100%').height('100%')
  }

  @Builder People() {
    List() {
      ForEach(this.rows, (row: ChatRow) => {
        ListItem() {
          Column() {
            Row() {
              Text(row.title).fontSize(16).fontColor($r('sys.color.ohos_id_color_text_primary')).layoutWeight(1)
              if (row.fp.length > 0) {
                Button(row.trust === 1 ? '已信任' : '信任')
                  .height(32).fontSize(13)
                  .enabled(row.trust !== 1)
                  .onClick(() => { this.toast(lsm.setTrust(row.fp, 1)); this.pull(); })
              }
            }.width('100%')
            Text(row.sub).fontSize(12).fontColor($r('sys.color.ohos_id_color_text_secondary'))
            if (row.fp.length > 0 && row.trust !== 1) {
              Button('PIN 配对').height(32).fontSize(13).margin({ top: 6 })
                .onClick(() => { this.peer = row.fp; this.field = lsm.pinOffer(); this.field2 = ''; this.sheet = 'pin'; })
            }
          }.alignItems(HorizontalAlign.Start).padding(14).width('100%')
        }
      }, (row: ChatRow) => 'p' + row.key)
    }.layoutWeight(1).width('100%').height('100%')
  }

  @Builder Files() {
    List() {
      ForEach(this.xfers, (t: Xfer) => {
        ListItem() {
          Column() {
            Text(t.name).fontSize(15).fontColor($r('sys.color.ohos_id_color_text_primary'))
            Text(t.status.length > 0 ? t.status : t.state).fontSize(12)
              .fontColor($r('sys.color.ohos_id_color_text_secondary'))
          }.alignItems(HorizontalAlign.Start).padding(14).width('100%')
        }.onClick(() => {
          if (t.path.length > 0 && this.isImage(t.name.length > 0 ? t.name : t.path)) {
            this.viewPath = t.path;
          }
        })
      }, (t: Xfer) => t.id)
    }.layoutWeight(1).width('100%').height('100%')
  }

  @Builder Settings() {
    Scroll() {
      Column() {
        Text(this.me.length > 0 ? this.me : '未命名').fontSize(20).fontWeight(FontWeight.Medium)
          .fontColor($r('sys.color.ohos_id_color_text_primary'))
        Text(this.fp).fontSize(12).fontColor($r('sys.color.ohos_id_color_text_secondary')).margin({ top: 4, bottom: 12 })
        Button('修改名字').width('100%').height(44).margin({ bottom: 8 })
          .onClick(() => { this.field = this.me; this.sheet = 'name'; })
        Button('中转服务器').width('100%').height(44).margin({ bottom: 8 })
          .onClick(() => { this.field2 = ''; this.sheet = 'relay'; })
        Button('我的链接').width('100%').height(44).margin({ bottom: 8 })
          .onClick(() => { this.field = lsm.myUri(); this.sheet = 'uri'; })
        Button('恢复密钥').width('100%').height(44).margin({ bottom: 8 })
          .onClick(() => { this.recovery = lsm.revealRecoveryKey(); this.sheet = 'key'; })
        Button('诊断').width('100%').height(44)
          .onClick(() => { this.diag = lsm.diagnostics(); this.sheet = 'diag'; })
        Text('协议 v5，与 0.7.4 的 Windows、Linux、Android 互通。图片在应用内查看。')
          .fontSize(12).fontColor($r('sys.color.ohos_id_color_text_secondary')).margin({ top: 16 })
      }.padding(16).alignItems(HorizontalAlign.Start).width('100%')
    }.layoutWeight(1).align(Alignment.Top).width('100%').height('100%')
  }

  @Builder Chat() {
    Column() {
      Row() {
        Button('返回').height(32).fontSize(14).onClick(() => { this.inChat = false; })
        Text(this.title).fontSize(18).fontWeight(FontWeight.Medium).margin({ left: 8 })
          .fontColor($r('sys.color.ohos_id_color_text_primary')).layoutWeight(1).maxLines(1)
      }.width('100%').padding({ left: 12, right: 12, top: 8 })
      if (this.conv.indexOf('dm:') === 0 && this.trust !== 1) {
        Text('尚未信任对方。点此信任，或完成 PIN 配对后才能发图片和文件。')
          .fontSize(13).fontColor($r('sys.color.ohos_id_color_text_primary'))
          .padding(12).margin(12).borderRadius(16).width('92%')
          .backgroundColor($r('sys.color.ohos_id_color_background'))
          .border({ width: 1, color: $r('sys.color.ohos_id_color_alert') })
          .onClick(() => { this.toast(lsm.setTrust(this.peer, 1)); this.pull(); })
      }
      List() {
        ForEach(this.msgs, (m: Msg) => {
          ListItem() {
            Column() {
              if (!m.out && m.sender.length > 0) {
                Text(m.sender).fontSize(11).fontColor($r('sys.color.ohos_id_color_text_secondary'))
              }
              Text(m.file.length > 0 ? m.file : m.body)
                .fontSize(15)
                .fontColor(m.out ? Color.White : $r('sys.color.ohos_id_color_text_primary'))
                .padding(12)
                .borderRadius(18)
                .backgroundColor(m.out ? $r('sys.color.ohos_id_color_emphasize') : $r('sys.color.ohos_id_color_background'))
                .onClick(() => {
                  if (m.path.length > 0 && this.isImage(m.file.length > 0 ? m.file : m.path)) {
                    this.viewPath = m.path;
                  }
                })
            }
            .alignItems(m.out ? HorizontalAlign.End : HorizontalAlign.Start)
            .padding({ left: 12, right: 12, top: 4, bottom: 4 })
            .width('100%')
          }
        }, (m: Msg) => m.id)
      }.layoutWeight(1).width('100%')
      Row() {
        Button('文件').height(40).onClick(() => { this.sendFile(); })
        TextInput({ text: this.draft, placeholder: '消息' })
          .layoutWeight(1).height(40).margin({ left: 8, right: 8 })
          .onChange((v: string) => { this.draft = v; })
        Button('发送').height(40).onClick(() => { this.send(); })
      }.width('100%').padding(12)
    }.width('100%').height('100%')
  }

  @Builder Viewer() {
    Column() {
      Row() {
        Button('关闭').onClick(() => { this.viewPath = ''; })
        Text('图片').fontSize(18).margin({ left: 12 }).fontColor($r('sys.color.ohos_id_color_text_primary'))
      }.width('100%').padding(12)
      Image(this.viewPath.indexOf('file:') === 0 ? this.viewPath : 'file://' + this.viewPath)
        .width('100%').layoutWeight(1).objectFit(ImageFit.Contain)
    }.width('100%').height('100%').backgroundColor($r('sys.color.ohos_id_color_sub_background'))
  }

  @Builder Sheet() {
    Column() {
      if (this.sheet === 'add') {
        Text('添加联系人').fontSize(18).margin({ bottom: 8 })
        TextInput({ text: this.field, placeholder: 'IP，或 lsm:// 链接' }).width('100%').onChange((v: string) => { this.field = v; })
        Button('添加').margin({ top: 12 }).onClick(() => {
          const v = this.field;
          const err = v.indexOf('lsm://') === 0 ? lsm.addUri(v) : lsm.addAddr(v);
          this.toast(err);
          if (err.length === 0) { this.sheet = ''; }
          lsm.refreshPeers();
          this.pull();
        })
      } else if (this.sheet === 'pin') {
        Text('我的 PIN：' + this.field).fontSize(16)
        TextInput({ text: this.field2, placeholder: '对方的 PIN' }).width('100%').margin({ top: 8 })
          .onChange((v: string) => { this.field2 = v; })
        Button('配对').margin({ top: 12 }).onClick(() => {
          this.toast(lsm.pinPair(this.peer, this.field2));
          this.sheet = '';
          this.pull();
        })
      } else if (this.sheet === 'name') {
        TextInput({ text: this.field, placeholder: '显示名' }).width('100%').onChange((v: string) => { this.field = v; })
        Button('保存').margin({ top: 12 }).onClick(() => { lsm.setName(this.field); this.sheet = ''; this.pull(); })
      } else if (this.sheet === 'relay') {
        Text('中转').fontSize(18)
        Toggle({ type: ToggleType.Switch, isOn: this.relayOn }).onChange((on: boolean) => { this.relayOn = on; })
        TextInput({ text: this.relayHost, placeholder: '主机' }).width('100%').margin({ top: 8 })
          .onChange((v: string) => { this.relayHost = v; })
        TextInput({ text: this.relayPort, placeholder: '端口' }).width('100%').margin({ top: 8 })
          .onChange((v: string) => { this.relayPort = v; })
        TextInput({ text: this.field2, placeholder: '口令，留空表示不改' }).type(InputType.Password).width('100%').margin({ top: 8 })
          .onChange((v: string) => { this.field2 = v; })
        Button('保存').margin({ top: 12 }).onClick(() => {
          const port = parseInt(this.relayPort);
          const err = lsm.relaySet(this.relayOn, this.relayHost, port > 0 ? port : 9443, this.field2.length > 0 ? this.field2 : null);
          this.toast(err);
          this.sheet = '';
          lsm.reconnectServer();
          this.pull();
        })
      } else if (this.sheet === 'uri') {
        Text(this.field).fontSize(13)
        Button('添加群').margin({ top: 12 }).onClick(() => { this.field = ''; this.field2 = ''; this.sheet = 'group'; })
      } else if (this.sheet === 'group') {
        TextInput({ text: this.field, placeholder: '群名' }).width('100%').onChange((v: string) => { this.field = v; })
        TextInput({ text: this.field2, placeholder: '成员指纹，逗号分隔' }).width('100%').margin({ top: 8 })
          .onChange((v: string) => { this.field2 = v; })
        Button('创建').margin({ top: 12 }).onClick(() => {
          const id = lsm.groupCreate(this.field, this.field2);
          this.toast(id.indexOf('!') === 0 ? id.substring(1) : '');
          this.sheet = '';
          this.pull();
        })
      } else if (this.sheet === 'key') {
        Text('恢复密钥只显示这一次，请抄下来。不会写入日志。').fontSize(13)
          .fontColor($r('sys.color.ohos_id_color_text_secondary'))
        Text(this.recovery.length > 0 ? this.recovery : '还没有恢复密钥').fontSize(16).margin({ top: 8 })
      } else if (this.sheet === 'diag') {
        Text(this.diag.length > 400 ? this.diag.substring(0, 400) : this.diag).fontSize(12)
      }
      Button('关闭').margin({ top: 16 }).onClick(() => { this.sheet = ''; this.recovery = ''; })
    }
    .width('92%')
    .padding(16)
    .borderRadius(24)
    .backgroundColor($r('sys.color.ohos_id_color_background'))
  }
}
