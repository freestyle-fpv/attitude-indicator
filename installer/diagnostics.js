(function(){
 'use strict';
 const {DiagnosticModel,SerialConnection,classify,makeReport,hardwareChecks,stationarySummary,reportFile}=window.InstrumentDiagnostics;
 const $=id=>document.getElementById(id),model=new DiagnosticModel({maxHistory:440});
 let paused=false,exampleTimer=null,renderQueued=false,viewError='',lastConsoleSignature='',lastConsolePaint=0,lastHardwarePaint=0,streamHeartbeat=0,stationarySince=null,pendingTest=null;
 const number=(value,places=0)=>value===null||value===undefined?'--':Number(value).toFixed(places);
 const serialSupported=window.isSecureContext&&navigator.serial&&typeof navigator.serial.requestPort==='function';
 const connection=new SerialConnection(navigator.serial,line=>{model.add(line);schedule();},(state,detail)=>{
   if(state==='connected'){streamHeartbeat=0;stopExample();if(detail?.resumed)model.resetTelemetry();else model.clear();resetView();viewError='';}
   $('connect').hidden=state==='connected'||state==='installing';$('disconnect').hidden=state!=='connected';
   $('connect').disabled=!serialSupported||state==='connecting'||state==='installing';
   $('connect').textContent=state==='connecting'?'正在连接…':'连接设备';
   $('connectionLabel').textContent={connected:'设备已连接',connecting:'正在选择设备',disconnected:'未连接设备',installing:'正在安装 · 诊断已暂停'}[state];
   $('connectionDot').classList.toggle('active',state==='connected');
   $('installFirmware').disabled=state==='connecting'||state==='installing';
   schedule();
 });
 if(!serialSupported){
   $('connect').disabled=true;
   $('connectionHelp').textContent='当前浏览器或页面环境不支持 USB 串口。请在电脑 Chrome / Edge 打开解压后的 HTML，或使用 HTTPS / localhost。仍可导入日志和查看示例。';
 }
 function resetView(){$('testNotice').textContent='先固定静止观察；IMU 自检会暂停姿态采样，结束后恢复配置并重新校准。';$('testNotice').dataset.tone='neutral';stationarySince=null;pendingTest=null;lastHardwarePaint=0;paused=false;lastConsoleSignature='';$('pause').textContent='暂停滚动';}
 function stopExample(){if(exampleTimer){clearInterval(exampleTimer);exampleTimer=null;}}
 function schedule(){if(!renderQueued){renderQueued=true;requestAnimationFrame(()=>{renderQueued=false;render();});}}
 function gyroFresh(){return model.source!=='live'||(connection.state==='connected'&&model.gyroAt!==null&&Date.now()-model.gyroAt<=3500&&model.telemetry.fault!==true&&(model.telemetry.age===null||model.telemetry.age<=100));}
 function graph(){
   const history=gyroFresh()?model.history.filter(row=>row.gyro&&(model.source!=='live'||Date.now()-row.time<10000)):[],scale=Math.max(5,...history.flatMap(row=>row.gyro.map(Math.abs)))*1.2;
   $('chartEmpty').hidden=history.length>0;$('chartEmpty').textContent=model.gyroAt!==null&&!gyroFresh()?'陀螺仪数据未更新':'等待陀螺仪数据';$('chartMax').textContent='+'+Math.ceil(scale);$('chartMin').textContent='−'+Math.ceil(scale);
   for(const [index,axis] of ['X','Y','Z'].entries()){
     const path=history.map((row,i)=>`${i?'L':'M'}${(i*650/Math.max(1,history.length-1)).toFixed(2)},${(66-row.gyro[index]/scale*62).toFixed(2)}`).join(' ');
     $('trace'+axis).setAttribute('d',path);
   }
 }
 function render(){
   const now=Date.now(),t=model.telemetry,state=connection.state==='installing'?{tone:'neutral',title:'安装窗口正在使用串口',description:'日志采集已暂停，关闭安装窗口后会自动恢复，无需重新选择设备。',code:'installing'}:classify(model,now,connection.state);
   $('analysis').dataset.tone=viewError?'warning':state.tone;
   $('analysisTitle').textContent=viewError?'暂时无法连接设备':state.title;
   $('analysisDescription').textContent=viewError||state.description;
   $('analysisIcon').textContent=state.tone==='success'?'✓':state.tone==='danger'||state.tone==='warning'?'!':'◇';
   $('stateCode').textContent=(viewError?'CONNECTION ERROR':state.code.replace(/_/g,' ')).toUpperCase();
   $('progress').textContent=t.count===null?'--':Math.floor(t.progress);
   $('progressArc').style.strokeDashoffset=String(515.22*(1-t.progress/100));
   $('sampleCount').textContent=t.count===null?'等待样本 / 400':`${t.count} / ${t.total} SAMPLES`;
   $('calibrationHint').textContent=t.count>=t.total?'稳定样本收集完成':'保持静止，进度回退时将重新累积';
   for(const [id,key] of [['peak','peak'],['reject','reject'],['invalid','invalid'],['gaps','gaps'],['drops','drops']])$(id).textContent=number(t[key]);
   ['X','Y','Z'].forEach((axis,i)=>$('gyro'+axis).textContent=number(gyroFresh()&&t.gyro?t.gyro[i]:null,2));
   $('gravity').textContent=number(t.gravity,3);$('sampleHz').textContent=number(t.hz,0);$('fps').textContent=number(t.fps,1);
   $('dataAge').textContent=model.lastTelemetryAt===null?'--':model.source==='live'?((now-model.lastTelemetryAt)/1000).toFixed(1):model.source==='example'?'示例':'历史';
   $('ageUnit').hidden=model.source!=='live';
   $('calibrate').disabled=connection.state!=='connected';$('export').disabled=!model.logs.length;
   $('sourceNotice').hidden=model.source==='live';
   $('sourceTitle').textContent=model.source==='example'?'示例数据 · 未连接设备':'历史日志 · 非实时设备数据';
   $('sourceDescription').textContent=model.source==='example'?'以下为模拟的速率超限场景，用于了解诊断页面。连接实机时会清空示例。':'仅分析导入文件，连接实机后开始新的采集。';
   $('example').disabled=connection.state!=='disconnected';$('importLog').disabled=connection.state!=='disconnected';
   $('example').textContent=model.source==='example'?'退出示例':'查看示例';
   $('lineCount').textContent=`${model.logs.length} LINES`;
   const elapsed=model.startedAt===null?0:Math.max(0,Math.floor((now-model.startedAt)/1000));
   $('captureState').textContent=model.logs.length?`${model.source==='live'?'已采集':model.source==='example'?'示例':'已导入'} ${model.logs.length} 条 · ${model.source==='imported'?'历史记录':elapsed+'s'}${model.omitted?' · 容量限制省略 '+model.omitted+' 条':''}`:'尚未开始采集';
   if(!paused&&(now-lastConsolePaint>=250||!lastConsoleSignature)){
     lastConsolePaint=now;
     const rows=$('onlyDiagnostic').checked?model.logs.filter(row=>/^(IMUDIAG|IMUFAST|IMUHW|BOARD|INPUT|DISPLAY|MEMORY|SELFTEST|PERIPHERAL|SDCHECK) /.test(row.text)):model.logs;
     const signature=model.logs.length+':'+model.omitted+':'+$('onlyDiagnostic').checked+':'+(model.logs.at(-1)?.time||0);
     if(signature!==lastConsoleSignature){
       $('console').textContent=rows.length?rows.slice(-300).map(row=>new Date(row.time).toLocaleTimeString('zh-CN',{hour12:false})+'  '+row.text).join('\n'):model.logs.length?'当前记录中没有 IMUDIAG 诊断行。':'尚未收到日志。连接开发板，或导入此前保存的日志文件。';
       $('console').scrollTop=$('console').scrollHeight;lastConsoleSignature=signature;
     }
   }
   const accel=t.accel;$('accelRaw').textContent=gyroFresh()&&accel?accel.map(v=>number(v,3)).join(' / '):'-- / -- / --';
   $('imuTemperature').textContent=number(model.source!=='live'||now-(model.hardwareAt.imu||0)<=3500?model.hardware.imu?.temp_c:null,2)+' °C';
   const live=connection.state==='connected',busy=!!pendingTest||model.hardware.selftest?.running===1;
   $('imuSelfTest').disabled=!live||busy;$('memoryTest').disabled=!live||busy;$('sdTest').disabled=!live||busy;
   $('stationaryTest').disabled=!live||stationarySince!==null||busy;
   const recent=model.history.filter(row=>now-row.time<=1000).length;
   $('refreshRate').textContent=live?`实时数据 · 最近 1 秒 ${recent} 次更新`:'连接后启用 20 Hz 实时数据';
   if(stationarySince!==null){
     const left=Math.max(0,10-(now-stationarySince)/1000);
     $('testNotice').textContent=left>0?`请保持完全静止，正在采集网页样本：还需 ${left.toFixed(1)} 秒。`:stationarySummary(model,stationarySince,now).text;
     if(left<=0){model.stationaryResult=stationarySummary(model,stationarySince,now);$('testNotice').dataset.tone=model.stationaryResult.tone;stationarySince=null;}
   }
   if(pendingTest){
     const result=model.hardware[pendingTest.kind];
     if(result&&result.id>pendingTest.id&&result.running!==1&&result.pending!==1){
       $('testNotice').textContent=pendingTest.kind==='selftest'?'IMU 自检已结束，请查看硬件结果；传感器配置已尝试恢复，校准将重新进行。':pendingTest.kind==='sd'?'SD 卡识别已结束，请查看硬件结果。':'内存与程序检查已结束，请查看硬件结果。';pendingTest=null;
     }else if(now-pendingTest.at>15000){$('testNotice').textContent='未在 15 秒内收到检测结果。请检查诊断固件版本、数据连接和下方原始日志。';$('testNotice').dataset.tone='warning';pendingTest=null;}
   }
   if(now-lastHardwarePaint>500){
     lastHardwarePaint=now;const checks=hardwareChecks(model,now,connection.state);
     const labels={success:'检查通过',neutral:'待检测',warning:'需要排查',danger:'检测异常'};
     $('hardwareResults').replaceChildren(...checks.map(check=>{
       const card=document.createElement('article');card.className='hardware-check';card.dataset.tone=check.tone;
       const head=document.createElement('header'),title=document.createElement('h3'),badge=document.createElement('span'),description=document.createElement('p');
       title.textContent=check.title;badge.className='badge';badge.textContent=check.badge||(check.tone==='success'?({identity:'配置正常',bus:'计数正常',temperature:'读数有效',selftest:'自检通过',power:'读数有效',input:'接口可用',display:'接口可用',memory:'抽检通过',system:'状态已读',rtc:'已观察到走时',audio:'接口可读',sd:'只读识别通过'})[check.id]:labels[check.tone]);description.textContent=check.description;
       head.append(title,badge);card.append(head,description);return card;
     }));
     $('hardwareSummary').textContent=`${checks.filter(c=>c.tone==='success').length} 项正常 · ${checks.filter(c=>c.tone==='warning'||c.tone==='danger').length} 项提示 · ${checks.filter(c=>c.tone==='neutral').length} 项未判定`;
   }
   graph();
 }
 // A lease stops the high-rate stream automatically if the page disappears.
 function heartbeat(){if(connection.state==='connected')connection.send('J').catch(()=>{});}
 setInterval(()=>{if(connection.state==='connected'&&Date.now()-streamHeartbeat>5000){streamHeartbeat=Date.now();heartbeat();}},500);
 $('stationaryTest').addEventListener('click',()=>{stationarySince=Date.now();$('testNotice').dataset.tone='neutral';schedule();});
 for(const [id,key,kind] of [['imuSelfTest','S','selftest'],['memoryTest','M','memory'],['sdTest','N','sd']])$(id).addEventListener('click',async()=>{
   pendingTest={kind,id:model.hardware[kind]?.id||0,at:Date.now()};stationarySince=null;
   $('testNotice').dataset.tone='neutral';$('testNotice').textContent=kind==='selftest'?'正在运行 IMU 内置自检，请保持静止。姿态采样将暂时暂停，结束后重新校准。':kind==='sd'?'正在只读识别 SD 卡；不格式化、不写文件。':'正在检查临时内存区域和运行程序镜像，请稍候。';schedule();
   try{await connection.send(key);}catch(error){pendingTest=null;viewError=error.message;schedule();}
 });
 $('connect').addEventListener('click',async()=>{
   viewError='';try{await connection.connect();}catch(error){
     if(error.name==='NotFoundError')viewError='已取消选择串口，可重新点击连接设备。';
     else if(error.name==='SecurityError')viewError='页面没有串口访问权限。请使用电脑 Chrome / Edge 直接打开文件，或使用 HTTPS / localhost。';
     else viewError='连接失败：'+error.message+'。请关闭其他串口工具，重新插拔后再连接。';
     schedule();
   }
 });
 $('disconnect').addEventListener('click',async()=>{await connection.disconnect();schedule();});
 $('calibrate').addEventListener('click',async()=>{
   try{await connection.calibrate();model.add('[网页操作] 已发送重新校准指令，请保持静止。');}
   catch(error){viewError=error.message;}schedule();
 });
 function exitSource(){stopExample();model.clear();viewError='';resetView();schedule();}
 $('exitSource').addEventListener('click',exitSource);
 $('example').addEventListener('click',()=>{
   if(model.source==='example'){exitSource();return;}
   stopExample();model.clear();resetView();model.source='example';viewError='';const now=Date.now();
   function sample(i,time){
     const x=11.25+Math.sin(i*.5)*.11,y=-.5+Math.sin(i*.7)*.03,z=.125+Math.cos(i*.6)*.02;
     model.add(`IMUFAST seq=${i} present=1 fault=0 valid=0 accel_g=-0.298,-0.375,0.917 cal=0/400 peak=4 reject=${600+i} reason=gyro_rate gyro_dps=${x.toFixed(3)},${y.toFixed(3)},${z.toFixed(3)} g=1.035 hz=200.0 invalid=0 gaps=0 age_ms=5`,time);
   }
   for(let i=0;i<200;i++)sample(i,now-(199-i)*50);
   model.add('fps=18.9 pitch=0.0 roll=0.0 valid=0 cal=0 raw_g=-0.298,-0.375,0.917 sample_hz=200.0 drops=200',now);
   model.add('IMUHW who=5 rev=1 ctrl2=21 ctrl3=101 ctrl7=3 temp_c=28.5 nack=0 short=0 sat=0 notready=12 repeats=0 timestamp=123 restore=1',now);
   model.add('BOARD pmu=1 adc=29 touch=1 expander=1 panel=1 battery=0 vbus_good=1 vbus_mv=5010 sys_mv=3950 heap=184000 heap_min=172000 psram=8388608 psram_free=6700000 flash=16777216 nvs=1 reset=1',now);
   model.add('INPUT touch_packets=12 touch_errors=0 boot_clicks=1 pwr_clicks=2',now);
   model.add('DISPLAY fps=40 panel=1 te_us=16666 te_pulses=1200 te_miss=0',now);
   let i=200;exampleTimer=setInterval(()=>{sample(i++,Date.now());schedule();},50);schedule();
 });
 $('importLog').addEventListener('click',()=>$('logFile').click());
 $('logFile').addEventListener('change',async event=>{
   const file=event.target.files[0];if(!file)return;
   if(file.size>2*1024*1024){viewError='日志文件超过 2 MB，请选取最近的一段日志后导入。';schedule();return;}
   try{
     const text=await file.text();stopExample();model.importText(text);viewError='';
     resetView();schedule();
   }catch(error){viewError='无法读取日志文件：'+error.message;schedule();}
   event.target.value='';
 });
 $('pause').addEventListener('click',()=>{paused=!paused;$('pause').textContent=paused?'继续滚动':'暂停滚动';if(!paused)lastConsoleSignature='';schedule();});
 $('onlyDiagnostic').addEventListener('change',()=>{lastConsoleSignature='';schedule();});
 $('clear').addEventListener('click',()=>{const source=model.source;model.clear();model.source=source;resetView();schedule();});
 let reportUrl=null;
 function exportStatus(text){$('exportStatus').hidden=false;$('exportStatus').textContent=text;}
 $('export').addEventListener('click',()=>{
   try{
     const file=reportFile(model,Date.now(),{browser:navigator.userAgent,protocol:location.protocol,connection:connection.state});
     if(reportUrl)URL.revokeObjectURL(reportUrl);
     reportUrl=URL.createObjectURL(new Blob([file.content],{type:'text/plain;charset=utf-8'}));
     $('reportContents').value=file.content;$('reportFilename').textContent=file.filename;
     $('reportDownload').href=reportUrl;$('reportDownload').download=file.filename;
     $('reportFeedback').textContent=`报告已生成，包含 ${file.rows} 条日志。已请求浏览器下载，请查看下载列表或系统下载文件夹；也可再次下载或复制内容。`;
     exportStatus('报告已生成：'+file.filename+'。请查看下载文件夹，或在预览中再次下载/复制。');
     if(!$('reportDialog').open)$('reportDialog').showModal();
     $('reportDownload').click();
   }catch(error){exportStatus('报告导出未完成：'+(error.message||error)+'。请保留当前页面和日志后重试。');}
 });
 $('reportDownload').addEventListener('click',()=>{$('reportFeedback').textContent='已请求浏览器下载。请查看浏览器下载列表或系统下载文件夹；没有文件时可复制下方报告内容。';});
 $('reportClose').addEventListener('click',()=>$('reportDialog').close());
 $('reportCopy').addEventListener('click',async()=>{
   try{
     if(navigator.clipboard?.writeText)await navigator.clipboard.writeText($('reportContents').value);
     else{ $('reportContents').focus();$('reportContents').select();if(!document.execCommand('copy'))throw new Error('请选中报告后手动复制'); }
     $('reportFeedback').textContent='报告已复制，可粘贴到文本文件或反馈消息中。';
   }catch(error){$('reportContents').focus();$('reportContents').select();$('reportFeedback').textContent='自动复制未完成。已选中报告，请按 Ctrl+C（Mac 为 Command+C）复制。';}
 });
 window.addEventListener('pagehide',()=>{if(reportUrl)URL.revokeObjectURL(reportUrl);});
 // One SerialConnection owns both the diagnostic reader and installer lease.
 $('installFirmware').addEventListener('click',async()=>{
   const status=$('installStatus');status.hidden=false;status.textContent='正在准备安装窗口…';
   try{
     await connection.install(port=>new Promise((resolve,reject)=>{
       try{
         const dialog=document.createElement('ewt-install-dialog');
         dialog.port=port;dialog.manifestPath=window.installerManifest;
         dialog.addEventListener('closed',()=>resolve(),{once:true});
         document.body.appendChild(dialog);
         status.textContent='设备已连接。请在安装窗口中确认操作。';
       }catch(error){reject(error);}
     }));
     status.textContent='安装窗口已关闭，设备连接已复用。可直接切换到「设备诊断」。';
   }catch(error){
     status.textContent=error.name==='NotFoundError'?'已取消选择设备，点击安装固件可重试。':'安装窗口无法打开：'+error.message+'。请检查 USB 连接及其他串口工具。';
   }
   schedule();
 });
 window.addEventListener('pagehide',()=>{stopExample();connection.disconnect().catch(()=>{});});
 setInterval(schedule,100);render();
})();
