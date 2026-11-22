(function(root,factory){
  if(typeof module==='object'&&module.exports)module.exports=factory();
  else root.InstrumentDiagnostics=factory();
})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const numeric=value=>value!==undefined&&value!==''&&Number.isFinite(Number(value))?Number(value):null;
  const integer=value=>{const n=numeric(value);return n!==null&&n>=0&&Number.isInteger(n)?n:null;};
  const triple=value=>{if(!value)return null;const a=value.split(',').map(numeric);return a.length===3&&a.every(x=>x!==null)?a:null;};
  const reasons={
    gyro_rate:['陀螺仪速率超限','最近一次校准样本超过了 5°/s 限制。请固定静止观察；持续出现时导出日志，进一步判断零偏或异常读数。'],
    gyro_variation:['陀螺仪读数变化较大','相邻采样块的陀螺仪变化超过稳定性条件。先排除晃动、桌面振动与 USB 线牵拉，再保留日志排查。'],
    accel_motion:['重力方向发生变化','校准检测到加速度向量变化。固定板子并等待一个完整的稳定窗口。'],
    gravity_range:['加速度超出校准范围','加速度模长不在 0.85–1.15g 内。固定静止后若持续超限，请导出日志进一步排查。'],
    nonfinite:['收到非有限传感器数据','加速度或陀螺仪中存在 NaN / Infinity。需要检查传感器读取情况。']
  };
  class LineDecoder{
    constructor(onLine,maxLength=8192){this.onLine=onLine;this.maxLength=maxLength;this.decoder=new TextDecoder();this.buffer='';this.overflow=false;this.dropped=0;}
    push(bytes){
      const text=this.decoder.decode(bytes,{stream:true});
      for(const chunk of text.split(/(?<=\n)/)){
        if(!this.overflow)this.buffer+=chunk;
        if(this.buffer.length>this.maxLength){this.buffer='';this.overflow=true;this.dropped++;}
        if(chunk.endsWith('\n')){
          if(!this.overflow){const line=this.buffer.replace(/[\r\n]+$/,'');if(line)this.onLine(line);}
          this.buffer='';this.overflow=false;
        }
      }
    }
  }
  class DiagnosticModel{
    constructor({maxLines=2000,maxHistory=120}={}){this.maxLines=maxLines;this.maxHistory=maxHistory;this.clear();}
    resetTelemetry(){this.stationaryResult=null;this.imuTimestampStaticAt=null;this.telemetry={gyro:null,gravity:null,progress:0,count:null,total:400,present:null,fault:null,valid:null,hz:null,fps:null,invalid:null,gaps:null,peak:null,reject:null,reason:'none',drops:null,age:null};this.hardware={imu:null,board:null,input:null,display:null,memory:null,selftest:null,peripheral:null,sd:null};this.hardwareAt={};this.lastSequence=null;this.history=[];this.lastTelemetryAt=null;this.gyroAt=null;this.diagnostic=false;}
    clear(){this.resetTelemetry();this.logs=[];this.startedAt=null;this.omitted=0;this.bytes=0;this.source='live';}
    importText(text,now=Date.now()){
      this.clear();this.source=/^数据来源：示例数据/m.test(text)?'example':'imported';
      const lines=String(text).split(/\r?\n/);let index=0;
      const decoder=new LineDecoder(line=>{
        const stamp=/^(\d{4}-\d\d-\d\dT[\d:.]+Z)\s+/.exec(line);
        const time=stamp?Date.parse(stamp[1]):NaN;
        this.add(stamp?line.slice(stamp[0].length):line,Number.isFinite(time)?time:now-lines.length+index++);
      });
      decoder.push(new TextEncoder().encode(text+'\n'));
      return this.logs.length;
    }
    add(line,now=Date.now()){
      line=String(line).slice(0,8192);if(this.startedAt===null)this.startedAt=now;
      this.logs.push({time:now,text:line});this.bytes+=line.length;
      while(this.logs.length>this.maxLines||this.bytes>1024*1024){this.bytes-=this.logs.shift().text.length;this.omitted++;}
      if(/^(ESP-ROM:|rst:|ATTITUDE \(IMU\))/.test(line))this.resetTelemetry();
      const fast=/^IMUFAST\s/.test(line),diagnostic=/^IMUDIAG\s/.test(line)||fast,performance=/^fps=/.test(line);
      const kind={IMUHW:'imu',BOARD:'board',INPUT:'input',DISPLAY:'display',MEMORY:'memory',SELFTEST:'selftest',PERIPHERAL:'peripheral',SDCHECK:'sd'}[line.split(' ',1)[0]];
      if(!diagnostic&&!performance&&!kind)return false;
      const values=Object.create(null);
      for(const match of line.matchAll(/(?:^|\s)([a-zA-Z_][\w]*)=([^\s]+)/g))values[match[1]]=match[2];
      if(kind){
        const allowed={imu:'who alt_who rev ctrl2 ctrl3 ctrl7 temp_c nack short sat notready repeats timestamp restore',board:'pmu adc touch expander panel battery vbus_good vbus_mv sys_mv batt_mv heap heap_min heap_largest psram psram_free flash reset nvs tx_drop',input:'touch_packets touch_errors x y contacts swipes boot_clicks pwr_clicks boot_down pwr_down button_ready',display:'fps panel te_us te_pulses te_miss',memory:'id ram ram_bytes psram psram_bytes flash pending',selftest:'id running accel gyro restore accel_mg gyro_dps',peripheral:'rtc rtc_sec rtc_os rtc_stop rtc_tick dac mic',sd:'id pending result bytes'}[kind].split(' ');
        const record=Object.create(null);let fields=0;
        for(const key of allowed){const value=key==='accel_mg'||key==='gyro_dps'?triple(values[key]):numeric(values[key]);if(value!==null){record[key]=value;fields++;}}
        if(!fields)return false;
        if(kind==='imu'){
          if(record.timestamp!==undefined&&record.timestamp===this.hardware.imu?.timestamp){if(this.imuTimestampStaticAt===null)this.imuTimestampStaticAt=this.hardwareAt.imu;}
          else this.imuTimestampStaticAt=null;
        }
        this.hardware[kind]=record;this.hardwareAt[kind]=now;
        if(kind==='display'&&record.fps>=0)this.telemetry.fps=record.fps;
        return true;
      }
      const t=this.telemetry;let changed=false;
      const number=(key,field,convert=numeric)=>{const n=convert(values[key]);if(n!==null){t[field]=n;changed=true;}};
      const flag=(key,field)=>{const n=numeric(values[key]);if(n===0||n===1){t[field]=!!n;changed=true;}};
      if(diagnostic){
        flag('present','present');flag('fault','fault');flag('valid','valid');
        const parts=(values.cal||'').split('/'),count=integer(parts[0]),total=integer(parts[1]);
        if(count!==null&&total!==null&&total>0&&total<=100000&&count<=total){t.count=count;t.total=total;t.progress=100*count/total;if(count<total)t.valid=false;changed=true;}
        const gyro=triple(values.gyro_dps);const newSample=!fast||values.seq!==this.lastSequence;
        if(gyro){t.gyro=gyro;if(newSample)this.gyroAt=now;changed=true;}
        const acceleration=triple(values.accel_g);if(acceleration)t.accel=acceleration;
        number('g','gravity');number('hz','hz');number('peak','peak',integer);number('reject','reject',integer);
        number('invalid','invalid',integer);number('gaps','gaps',integer);number('age_ms','age',integer);
        if(values.reason==='none'||Object.hasOwn(reasons,values.reason)){t.reason=values.reason;changed=true;}
        if(changed)this.diagnostic=true;
      }else{
        flag('valid','valid');
        if(values.valid==='1'){t.present=true;t.fault=false;}
        number('fps','fps');number('sample_hz','hz');number('drops','drops',integer);
        const count=integer(values.cal);if(count!==null&&count<=400){t.count=count;t.total=400;t.progress=count/4;changed=true;}
        const a=triple(values.raw_g);if(a){t.gravity=Math.hypot(...a);changed=true;}
      }
      if(changed){
        this.lastTelemetryAt=now;
        if(diagnostic&&triple(values.gyro_dps)&&(!fast||values.seq!==this.lastSequence)){this.history.push({time:now,gyro:t.gyro.slice(),accel:t.accel?.slice()||null,gravity:t.gravity,progress:t.progress});}
        if(fast)this.lastSequence=values.seq;
        if(this.history.length>this.maxHistory)this.history.shift();
      }
      return changed;
    }
  }
  function classify(model,now,connection){
    const status=(code,tone,title,description)=>({code,tone,title,description});
    if(connection==='connecting')return status('connecting','neutral','正在连接设备','选择开发板对应串口，网页将以 115200 波特率读取。');
    if(model.source==='live'&&connection!=='connected')return status('disconnected','neutral','等待连接开发板','用 USB 数据线连接开发板，关闭其他串口工具，再点击连接设备。');
    if(model.lastTelemetryAt===null){
      if(model.source!=='live')return status('unrecognized','warning','未识别到仪表数据','请选择包含 IMUDIAG 或 fps= 行的仪表串口日志。');
      return status('waiting','neutral','等待设备日志','串口已打开。请等待数据；无输出时可检查端口、数据线与其他程序占用情况。');
    }
    if(model.source==='live'&&now-model.lastTelemetryAt>3500)return status('stale','warning','数据已停止更新','当前保留的是历史读数。请检查 USB 连接；需要时断开后重新连接。');
    const t=model.telemetry;
    if(model.hardware.selftest?.running===1&&now-(model.hardwareAt.selftest||0)<3500)return status('self_testing','neutral','IMU 内置自检进行中','姿态采样暂时暂停；测试结束后恢复配置并重新校准。');
    if(t.present===false)return status('sensor_offline','danger','传感器尚未就绪','IMU 初始化未成功。保留启动日志和当前诊断信息，检查板型和传感器连接。');
    if(t.fault===true)return status('sample_fault','danger','传感器读取或采样异常','观察失效读数、采样中断和数据频率，导出日志用于进一步定位。');
    if(t.valid===true)return status('ready','success','校准完成，姿态有效','已收到固件的有效姿态标记。网页继续监视传感器与采样状态。');
    if(t.count>=t.total)return status('calibrated','neutral','校准样本已收集完成','等待固件输出有效姿态；如持续没有有效数据，请保留日志。');
    if(!model.diagnostic)return status('legacy','warning','校准尚未完成，缺少详细诊断','已识别旧版日志。要查看陀螺仪与具体拒绝原因，请先安装本包中的诊断固件。');
    if(t.count>0)return status('collecting','success','正在收集静止样本','请保持开发板静止。进度回退表示稳定窗口重新开始，不代表程序卡死。');
    if(reasons[t.reason])return status(t.reason,'warning',...reasons[t.reason]);
    return status('collecting','neutral','等待稳定样本','保持板子静止，观察进度和数据频率；持续为零时请导出日志。');
  }

  function stationarySummary(model,since,until){
    const rows=model.history.filter(row=>row.time>=since&&row.time<=until&&row.gyro&&row.gravity!==null);
    if(rows.length<10)return {tone:'neutral',text:'有效样本不足，不能判断静止稳定性。请安装诊断固件并重新采集。'};
    const mean=[0,1,2].map(i=>rows.reduce((sum,r)=>sum+r.gyro[i],0)/rows.length);
    const std=[0,1,2].map(i=>Math.sqrt(rows.reduce((sum,r)=>sum+(r.gyro[i]-mean[i])**2,0)/rows.length));
    const gravity=rows.map(r=>r.gravity),low=Math.min(...gravity),high=Math.max(...gravity),norm=Math.hypot(...mean);
    let advice='窗口内读数较稳定，继续结合固件拒绝原因与硬件自检排查。',tone='success';
    if(norm>5){advice='三轴平均合成速率超过 5°/s；确认完全静止后，重点排查陀螺仪零偏和异常读数。';tone='warning';}
    else if(low<=.85||high>=1.15){advice='加速度模长曾超出校准范围，检查振动、读数与供电。';tone='warning';}
    else if(Math.hypot(...std)>.75){advice='网页采集到明显波动，请排除桌面振动和线材牵拉；校准仍以固件条件为准。';tone='warning';}
    return {tone,mean,std,count:rows.length,text:`${rows.length} 个网页样本；陀螺仪均值 ${mean.map(x=>x.toFixed(2)).join(' / ')} °/s，标准差 ${std.map(x=>x.toFixed(2)).join(' / ')} °/s；加速度 ${low.toFixed(3)}–${high.toFixed(3)} g。${advice}`};
  }
  function hardwareChecks(model,now,connection){
    const h=model.hardware,rows=[];
    const fresh=kind=>h[kind]&&(model.source!=='live'||(connection==='connected'&&now-model.hardwareAt[kind]<=3500));
    const add=(id,title,tone,description)=>rows.push({id,title,tone,description});
    if(fresh('imu')){
      const i=h.imu;
      if(i.who===5)add('identity','IMU 芯片与配置',i.ctrl2===21&&i.ctrl3===101&&(i.ctrl7&3)===3?'success':'warning',`0x6B 身份 0x05，修订 ${i.rev??'未读取'}；配置 ${[i.ctrl2,i.ctrl3,i.ctrl7].map(v=>v===undefined?'未读取':'0x'+v.toString(16).toUpperCase()).join(' / ')}。`+((i.ctrl7&3)!==3?'加速度计或陀螺仪未同时启用。':''));
      else add('identity','IMU 芯片与配置','danger',i.alt_who===5?'预期 0x6B 未就绪，但 0x6A 发现 IMU。可能是板型或地址差异，当前固件不能直接按原配置使用。':'IMU 身份未读取成功或不匹配。检查板型、供电与 I²C 通信；不能据此单独认定芯片损坏。');
      const errors=(i.nack||0)+(i.short||0)+(i.sat||0),frozen=model.imuTimestampStaticAt!==null&&now-model.imuTimestampStaticAt>=2000&&h.selftest?.running!==1;
      add('bus','IMU 通信与读数',['nack','short','sat','notready','repeats'].every(key=>i[key]!==undefined)?errors||frozen?'warning':'success':'neutral',`累计：通信失败 ${i.nack??'未读取'}，短读 ${i.short??'未读取'}，饱和 ${i.sat??'未读取'}，未就绪 ${i.notready??'未读取'}，重复时间戳 ${i.repeats??'未读取'}。`+(frozen?'连续报告的传感器时间戳没有推进，疑似数据冻结；请导出日志。':errors?'计数包含历史事件，观察是否持续增长。':'未就绪和少量重复可发生在启动或测试切换时，不等于硬件故障。'));
      add('temperature','IMU 温度',i.temp_c===undefined?'neutral':i.temp_c< -40||i.temp_c>85?'warning':'success',i.temp_c===undefined?'尚未读取温度。':`${i.temp_c.toFixed(2)} °C；这是芯片内部读数，不能代替精度或温漂校准。`);
    }else for(const [id,title] of [['identity','IMU 芯片与配置'],['bus','IMU 通信与读数'],['temperature','IMU 温度']])add(id,title,'neutral','未取得新的检测数据。连接设备并安装当前诊断固件；旧记录不代表当前状态。');
    const st=fresh('selftest')?h.selftest:null;
    const result=code=>({'-1':'未检测',0:'响应低于手册阈值',1:'通过',3:'通信失败',4:'等待响应超时'})[code]||'未取得有效结果';
    add('selftest','IMU 内置自检',!st||!st.id||st.running===undefined?'neutral':st.running===1?'neutral':st.accel===1&&st.gyro===1&&st.restore===1?'success':'warning',!st||!st.id?'点击「IMU 自检」进行电静力响应检查；自检后恢复配置并重新校准。':st.running===1?'正在测试，姿态采样暂时暂停。':`加速度计：${result(st.accel)}；陀螺仪：${result(st.gyro)}；配置恢复：${st.restore===1?'成功':'未确认成功'}。`+(st.accel_mg?`加速度响应 ${st.accel_mg.map(x=>x.toFixed(1)).join(' / ')} mg；陀螺仪响应 ${st.gyro_dps?.map(x=>x.toFixed(1)).join(' / ')||'未读取'} °/s。`:'')+'自检失败也可能由通信或供电异常引起。');
    const b=fresh('board')?h.board:null;
    add('power','电源管理与电压',!b?'neutral':b.pmu===1?(b.vbus_good!==1||!(b.vbus_mv>0)||!(b.sys_mv>0)?'neutral':b.vbus_mv<4500||b.vbus_mv>5500?'warning':'success'):'warning',!b?'等待电源管理数据。':b.pmu!==1?'AXP2101 未就绪，检查供电与总线。':`AXP2101 可通信；USB ${b.vbus_mv>0?(b.vbus_mv/1000).toFixed(3)+' V':'未检测到有效电压'}，系统 ${b.sys_mv>0?(b.sys_mv/1000).toFixed(3)+' V':'未读取'}；${b.battery===0?'未连接电池，允许 USB 单独供电':b.battery===1?'电池 '+(b.batt_mv/1000).toFixed(3)+' V':'电池状态未读取'}。仅为低频 ADC，无法判断供电纹波。`);
    const input=fresh('input')?h.input:null;
    add('input','触摸与物理按键',!b?'neutral':b.touch===1&&b.expander===1?'success':'warning',!b?'等待触摸和按键接口数据。':`触摸控制器 ${b.touch===1?'可通信':'未就绪'}，按键扩展器 ${b.expander===1?'可通信':'未就绪'}。`+(input?`触摸包 ${input.touch_packets??0}，读取错误 ${input.touch_errors??0}，BOOT 单击 ${input.boot_clicks??0}，PWR 单击 ${input.pwr_clicks??0}。`:'')+'请手动触摸和按键核对事件增长；PWR 会启动校准，BOOT 会打开设置。');
    const display=fresh('display')?h.display:null;
    add('display','显示与同步信号',!display?'neutral':display.panel===1&&display.te_pulses>0?'success':'warning',!display?'等待显示状态。':`初始化 ${display.panel===1?'成功':'未成功'}；${display.fps>=0?display.fps.toFixed(1):'未读取'} FPS，TE 脉冲 ${display.te_pulses??0}，错过同步 ${display.te_miss??0}。请人工检查坏点、颜色和画面完整性。`);
    const mem=fresh('memory')?h.memory:null;
    add('memory','RAM / PSRAM / 程序镜像',!mem||!mem.id||mem.pending===undefined||mem.pending===1?'neutral':mem.ram===1&&mem.psram===1&&mem.flash===1?'success':'warning',mem?.pending===1?'正在运行检测，请稍候。':!mem||!mem.id?'点击「内存与程序检查」。只测试新分配的小块内存和运行程序镜像，不覆盖用户设置。':`内部 RAM ${mem.ram===1?'通过':mem.ram===2?'无法分配测试区':'未通过'}（${mem.ram_bytes||0} 字节），PSRAM ${mem.psram===1?'通过':mem.psram===2?'无法分配测试区':'未通过'}（${mem.psram_bytes||0} 字节）；运行程序镜像 ${mem.flash===1?'校验通过':'校验未通过'}。小块检查不能覆盖整颗存储器。`);
    add('system','系统资源与设置存储',!b||b.nvs===undefined?'neutral':b.nvs===0||[4,5,6,7,9,13,14,15].includes(b.reset)?'warning':'success',!b?'等待资源信息。':`可用堆 ${b.heap??'未读取'} 字节，最低 ${b.heap_min??'未读取'}；PSRAM ${b.psram??'未读取'} 字节；Flash ${b.flash??'未读取'} 字节；上次复位 ${({0:'原因未知',1:'上电',2:'外部复位',3:'软件重启',4:'程序异常',5:'中断看门狗',6:'任务看门狗',7:'其他看门狗',8:'深睡唤醒',9:'欠压',10:'SDIO',11:'USB',12:'JTAG',13:'eFuse 错误',14:'电源异常',15:'CPU 锁定'})[b.reset]||'未读取'}；设置存储 ${b.nvs===1?'已打开':b.nvs===0?'打开失败':'未读取'}。`);
    const p=fresh('peripheral')?h.peripheral:null;
    add('rtc','RTC 时钟',!p||p.rtc!==1?'neutral':p.rtc_os===1||p.rtc_stop===1||p.rtc_sec<0||p.rtc_tick===0?'warning':p.rtc_tick===1?'success':'neutral',!p?'等待 RTC 数据。':p.rtc!==1?'PCF85063 未响应，检查该板版本及供电。':`秒字段 ${p.rtc_sec??'未读取'}；${p.rtc_tick===1?'已观察到走时':p.rtc_tick===0?'多次巡检没有观察到走时':'等待走时观察'}。`+(p.rtc_os===1?'时钟报告过停振，时间可能失效；需设置时间或检查后备供电，不能单独认定硬件损坏。':p.rtc_stop===1?'当前设置了停走位，需要检查 RTC 配置。':'这里只检查接口和走时，不校验时钟精度。'));
    add('audio','音频芯片接口',p?.dac===1&&p?.mic===1?'success':'neutral',!p?'等待音频芯片接口数据。':`ES8311 播放接口 ${p.dac===1?'可读':'未响应或未启用'}；ES7210 麦克风接口 ${p.mic===1?'可读':'未响应或未启用'}。未更改音频供电或控制状态；扬声器、麦克风和录放音仍需人工测试。`);
    const sd=fresh('sd')?h.sd:null;
    add('sd','SD 卡只读识别',!sd||!sd.id||sd.pending!==0?'neutral':sd.result===1?'success':sd.result===0?'warning':'neutral',!sd||!sd.id?'点击「SD 卡只读识别」，读取卡信息和首扇区；不格式化、不写文件。':sd.pending!==0?'正在识别存储卡。':sd.result===1?`识别并读取成功，容量约 ${(sd.bytes/1073741824).toFixed(2)} GiB。仅检查首扇区可读，不代表整卡完整。`:sd.result===2?'未识别到存储卡：可能未插卡、接触不良或初始化失败；不能据此判定卡槽损坏。':'卡已识别，但读取首扇区失败，请检查存储卡和接触情况。');
    const rtcRow=rows.find(row=>row.id==='rtc');if(p?.rtc===1&&p.rtc_os===1)rtcRow.badge='时间需设置';
    const sdRow=rows.find(row=>row.id==='sd');if(sd?.id>0&&sd.pending===0&&sd.result===2)sdRow.badge='未识别存储卡';
    return rows;
  }
  function makeReport(model,now,environment={}){
    const source={live:'设备实时数据',example:'示例数据（不是实机诊断）',imported:'导入的历史日志'}[model.source];
    const state=classify(model,now,model.source==='live'?(environment.connection||'connected'):'history');
    return ['航模座舱仪表 · 诊断报告',`生成时间：${new Date(now).toISOString()}`,`数据来源：${source}`,
      `浏览器：${environment.browser||'未记录'}`,`页面协议：${environment.protocol||'未记录'}`,
      `连接状态：${environment.connection||'未记录'}`,`分析：${state.title}`,state.description,
      '以下分析用于定位，不能单凭阈值拒绝确诊硬件损坏。',
      '--- 硬件诊断 ---',...hardwareChecks(model,now,environment.connection||'connected').map(row=>row.title+'：'+row.description),
      `硬件原始结果：${JSON.stringify(model.hardware)}`,`静止窗口：${model.stationaryResult?.text||'未采集'}`,
      `指标：${JSON.stringify(model.telemetry)}`,`容量限制省略日志 ${model.omitted} 条。`,
      '--- 原始日志（含本地采集时间） ---',...model.logs.map(row=>`${new Date(row.time).toISOString()} ${row.text}`)].join('\n');
  }
  function reportFile(model,now,environment){
    const source={live:'设备实测',example:'示例',imported:'历史日志'}[model.source]||'日志';
    return {filename:`仪表诊断_${source}_${new Date(now).toISOString().replace(/[:.]/g,'-')}.txt`,content:makeReport(model,now,environment),rows:model.logs.length};
  }
  class SerialConnection{
    constructor(serial,onLine,onState=()=>{}){this.serial=serial;this.onLine=onLine;this.onState=onState;this.state='disconnected';this.port=null;this.reader=null;this.readTask=null;this.writeTask=null;this.closing=null;this.epoch=0;}
    setState(state,detail){this.state=state;this.onState(state,detail);}
    async connect(){
      if(this.state!=='disconnected')throw new Error('连接正在使用');
      const epoch=++this.epoch;this.setState('connecting');let port,opened=false;
      try{
        port=await this.serial.requestPort();
        await port.open({baudRate:115200,bufferSize:8192});opened=true;
        if(epoch!==this.epoch){await port.close();return;}
        this.port=port;this.closing=null;this.setState('connected');
        this.readTask=this.read(port);
      }catch(error){
        if(opened)try{await port.close();}catch(_){}
        this.port=null;this.setState('disconnected');throw error;
      }
    }
    async read(port){
      const lines=new LineDecoder(line=>this.onLine(line));
      try{
        if(!port.readable)throw new Error('串口没有可读数据流');
        this.reader=port.readable.getReader();
        for(;;){const {value,done}=await this.reader.read();if(done)break;if(value)lines.push(value);}
      }catch(error){this.onLine(`[串口读取结束] ${error.message||error}`);}
      finally{
        if(this.reader){this.reader.releaseLock();this.reader=null;}
        if(!this.handoff)await this.finish(port);
      }
    }
    async finish(port){
      if(this.closing)return this.closing;
      this.closing=(async()=>{
        if(this.writeTask)try{await this.writeTask;}catch(_){}
        try{await port.close();}catch(_){}
        if(this.port===port){this.port=null;this.setState('disconnected');}
      })();
      return this.closing;
    }
    async disconnect(){
      if(this.state==='installing')throw new Error('安装窗口使用中，请先关闭安装窗口');
      ++this.epoch;
      if(this.reader)try{await this.reader.cancel();}catch(_){}
      if(this.readTask)await this.readTask;
      else if(this.port)await this.finish(this.port);
      if(!this.port)this.setState('disconnected');
    }
    async install(openInstaller){
      if(this.state==='disconnected')await this.connect();
      if(this.state!=='connected')throw new Error('串口正在连接或安装，请稍候');
      const port=this.port;
      this.handoff=true;this.setState('installing');
      try{
        // Release every stream lock before ESP Web Tools takes ownership.
        if(this.reader)await this.reader.cancel();
        if(this.readTask)await this.readTask;
        if(this.writeTask)await this.writeTask;
        await port.close();
        await port.open({baudRate:115200,bufferSize:8192});
        await openInstaller(port);
      }finally{
        this.handoff=false;this.readTask=null;this.closing=null;
        try{
          // A cancelled ReadableStream cannot be read again. Reopen the same
          // authorized port so cancelling installation also restores live logs.
          if(port.readable||port.writable)await port.close();
          await port.open({baudRate:115200,bufferSize:8192});
          this.setState('connected',{resumed:true});this.readTask=this.read(port);
        }catch(error){
          try{await port.close();}catch(_){}
          this.port=null;this.setState('disconnected');throw error;
        }
      }
    }
    async send(command){
      if(this.state!=='connected'||!this.port||!this.port.writable)throw new Error('请先连接开发板');
      if(this.writeTask)throw new Error('上一条指令仍在发送');
      const writer=this.port.writable.getWriter();
      this.writeTask=(async()=>{try{await writer.write(new TextEncoder().encode(command));}finally{writer.releaseLock();}})();
      try{await this.writeTask;}finally{this.writeTask=null;}
    }
    async calibrate(){return this.send('c');}

  }
  return {LineDecoder,DiagnosticModel,SerialConnection,classify,makeReport,reasons,hardwareChecks,stationarySummary,reportFile};
});
