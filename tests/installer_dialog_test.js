// Exercise the real, pinned upstream dialog methods after our packaging transform.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const {SerialConnection}=require('../installer/diagnostics-core.js');
function html(strings,...values){
 const result={text:strings[0],clicks:[]};
 values.forEach((value,index)=>{
  if(typeof value==='function'){result.clicks.push(value);result.text+='[handler]';}
  else if(value&&typeof value.text==='string'){result.text+=value.text;result.clicks.push(...value.clicks);}
  else result.text+=value??'';
  result.text+=strings[index+1];
 });return result;
}
class Base extends EventTarget{
 _confirmInstall(){throw new Error('Flow must not flash without explicit final confirmation');}
 _renderProgress(label){return html`${label}`;}
 _initialize(){}
}
// Node 18 supports EventTarget but does not expose CustomEvent by default.
class FixtureCustomEvent extends Event{
 constructor(type,options={}){super(type,options);this.detail=options.detail;}
}
const Dialog=vm.runInNewContext(fs.readFileSync(process.argv[2],'utf8')+'\nDialog;',{
 Base,r:html,an:class{},nn:'!',console,window:{},document:{body:{style:{overflow:''}}},CustomEvent:FixtureCustomEvent
});
function fixture(){
 const dialog=new Dialog();dialog._client=null;dialog._manifest={name:'仪表测试',version:'test'};
 let removed=false,closes=0;const checkbox={checked:false};
 dialog.shadowRoot={querySelector(selector){
  if(selector==='ew-checkbox')return checkbox;
  assert.equal(selector,'ew-dialog');
  return {close(){closes++;dialog.closeTask=dialog._handleClose();}};
 }};
 dialog.parentNode={removeChild(node){assert.equal(node,dialog);removed=true;}};
 return {dialog,checkbox,get removed(){return removed;},get closes(){return closes;}};
}
function portFixture(){
 const port={readable:null,writable:null,
  async open(){this.readable=new ReadableStream({});this.writable=new WritableStream({});},
  async close(){assert.equal(this.readable?.locked||false,false);assert.equal(this.writable?.locked||false,false);this.readable=this.writable=null;}
 };return port;
}
(async()=>{
 for(const erase of [false,true]){
  const f=fixture(),d=f.dialog;
  assert.equal(d._state,'ASK_ERASE');
  let [title,view]=d._renderAskErase();assert.match(title,/清除设备数据/);assert.doesNotMatch(view.text,/串口日志/);
  f.checkbox.checked=erase;view.clicks.at(-1).call(d);
  assert.equal(d._state,'INSTALL');assert.equal(d._installErase,erase);assert.equal(d._installConfirmed,false);
  [title,view]=d._renderInstall();assert.match(title,/确认安装/);
  if(erase)assert.match(view.text,/全部数据都将被清除/);
  else assert.doesNotMatch(view.text,/全部数据都将被清除/);
  view.clicks[0].call(d);assert.equal(d._state,'ASK_ERASE','confirmation Back must not resurrect the dashboard');
 }
 const cancel=fixture();cancel.dialog._renderAskErase()[1].clicks[0].call(cancel.dialog);
 await cancel.dialog.closeTask;assert.equal(cancel.closes,1);assert.equal(cancel.removed,true);
 let choices=0;const port=portFixture(),connection=new SerialConnection({requestPort:async()=>{choices++;return port;}},()=>{});
 await connection.connect();
 await connection.install(async selected=>{
  assert.equal(selected,port);assert.equal(connection.state,'installing');
  const f=fixture(),d=f.dialog;d.port=selected;d._state='INSTALL';d._installConfirmed=true;d._installState={state:'finished'};
  const view=d._renderInstall()[1];assert.match(view.text,/安装完成/);assert.match(view.text,/>\s*关闭\s*</);assert.doesNotMatch(view.text,/下一步/);
  const closed=new Promise(resolve=>d.addEventListener('closed',resolve,{once:true}));
  view.clicks[0].call(d);await closed;await d.closeTask;
  assert.equal(f.closes,1);assert.equal(f.removed,true);
 });
 assert.equal(connection.state,'connected');assert.equal(choices,1,'closing completion must reuse the original serial authorization');
 await connection.disconnect();
 console.log('PASS direct erase/confirmation, cancel/back, completion close and diagnostic serial handoff');
})().catch(error=>{console.error(error);process.exitCode=1;});
