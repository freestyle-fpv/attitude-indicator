const test = require('node:test');
const assert = require('node:assert/strict');
const {LineDecoder, DiagnosticModel, SerialConnection, classify, makeReport} = require('../installer/diagnostics-core.js');
const blocked = 'IMUDIAG v=caldiag1 present=1 fault=0 cal=0/400 peak=4 reject=600 reason=gyro_rate gyro_dps=6.250,-0.500,0.125 g=1.035 hz=199.5 invalid=0 gaps=0 age_ms=5';
const quiet = 'IMUDIAG v=caldiag1 present=1 fault=0 cal=200/400 peak=200 reject=600 reason=gyro_rate gyro_dps=0.010,-0.020,0.010 g=1.001 hz=200.0 invalid=0 gaps=0 age_ms=2';
test('serial UTF-8 chunks, CRLF and split lines are decoded exactly once', () => {
 const lines=[]; const decoder=new LineDecoder(line=>lines.push(line));
 const bytes=new TextEncoder().encode('启动\r\n'+blocked+'\n');
 for(let i=0;i<bytes.length;i+=2) decoder.push(bytes.subarray(i,i+2));
 assert.deepEqual(lines,['启动',blocked]);
});
test('oversized unterminated lines are bounded and cannot become telemetry', () => {
 const lines=[]; const decoder=new LineDecoder(line=>lines.push(line),32);
 decoder.push(new TextEncoder().encode('x'.repeat(10000)+'\nvalid=1 cal=400\n'));
 assert.deepEqual(lines,['valid=1 cal=400']);
 assert.equal(decoder.dropped,1);
});
test('zero-progress diagnosis reports a rejection, not a proven bad sensor', () => {
 const model=new DiagnosticModel();model.add(blocked,1000);
 const state=classify(model,1001,'connected');
 assert.equal(state.code,'gyro_rate');assert.equal(state.tone,'warning');
 assert.match(state.title,/速率/);assert.doesNotMatch(state.title,/损坏|已确诊/);
 assert.deepEqual(model.telemetry.gyro,[6.25,-.5,.125]);
 assert.equal(model.telemetry.progress,0);assert.equal(model.telemetry.gravity,1.035);
});
test('a progressing window is collecting even when an old rejection is retained', () => {
 const model=new DiagnosticModel();model.add(blocked,1000);model.add(quiet,2000);
 assert.equal(classify(model,2001,'connected').code,'collecting');
 assert.equal(model.telemetry.progress,50);
});
test('legacy logs support progress and success without inventing a gyro diagnosis', () => {
 const model=new DiagnosticModel();model.add('fps=18.9 pitch=0.0 roll=0.0 valid=0 cal=0 raw_g=-0.298,-0.375,0.917 sample_hz=200.0 drops=200',1000);
 assert.equal(classify(model,1001,'connected').code,'legacy');
 assert.equal(model.telemetry.gyro,null);assert.equal(model.telemetry.invalid,null);
 assert.equal(model.telemetry.drops,200);
 model.add('fps=39.8 valid=1 cal=400 pitch=2.1 roll=-3.2 sample_hz=200.0',2000);
 assert.equal(classify(model,2001,'connected').code,'ready');
});
test('serial text, invalid numeric fields and nonfinite values cannot forge healthy data', () => {
 const model=new DiagnosticModel();model.add(blocked,1000);
 model.add('<img src=x onerror=alert(1)> valid=1',2000);
 assert.equal(classify(model,2001,'connected').code,'gyro_rate');
 model.add('IMUDIAG present=NaN fault=NaN cal=Infinity/400 gyro_dps=1,NaN,0 g=Infinity hz=NaN',2200);
 assert.equal(model.telemetry.gravity,1.035);assert.deepEqual(model.telemetry.gyro,[6.25,-.5,.125]);
 assert.equal(model.telemetry.progress,0);
});
test('stale or disconnected data is never marked currently healthy', () => {
 const model=new DiagnosticModel();model.add('fps=40 valid=1 cal=400',1000);
 assert.equal(classify(model,6000,'connected').code,'stale');
 assert.equal(classify(model,1100,'disconnected').code,'disconnected');
});
test('sensor faults take precedence over past successful calibration', () => {
 const model=new DiagnosticModel();model.add('fps=40 valid=1 cal=400',1000);
 model.add('IMUDIAG present=1 fault=1 cal=400/400 gyro_dps=0,0,0 g=1 hz=0 invalid=3 gaps=1 age_ms=700',2000);
 assert.equal(classify(model,2100,'connected').code,'sample_fault');
});
test('bounded capture and exported report keep example/imported data clearly labelled', () => {
 const model=new DiagnosticModel({maxLines:3,maxHistory:2});model.source='example';
 for(let i=0;i<8;i++)model.add(blocked,1000+i);
 assert.equal(model.logs.length,3);assert.equal(model.history.length,2);assert.equal(model.omitted,5);
 const report=makeReport(model,2000,{browser:'test',protocol:'file:'});
 assert.match(report,/示例数据/);assert.match(report,/IMUDIAG/);assert.match(report,/省略.*5/);
 assert.doesNotMatch(report,/file:\/\/Users/);
});
function fakePort(){
 let controller;const writes=[];const calls=[];
 const port={readable:new ReadableStream({start(c){controller=c;}}),
 writable:new WritableStream({write(bytes){writes.push(new TextDecoder().decode(bytes));}}),
 async open(options){calls.push(options);},async close(){calls.push('closed');},getInfo(){return {usbVendorId:123};}};
 return {port,writes,calls,send(text){controller.enqueue(new TextEncoder().encode(text));},unplug(){controller.close();}};
}
test('manual serial connection opens at 115200 and cancel releases reader before port close', async () => {
 const fixture=fakePort();const lines=[];
 const connection=new SerialConnection({requestPort:async()=>fixture.port},line=>lines.push(line));
 await connection.connect();fixture.send(blocked+'\n');
 await new Promise(resolve=>setImmediate(resolve));
 assert.equal(lines[0],blocked);assert.equal(fixture.calls[0].baudRate,115200);
 await connection.calibrate();assert.deepEqual(fixture.writes,['c']);
 await connection.disconnect();assert.equal(fixture.port.readable.locked,false);
 assert.equal(fixture.port.writable.locked,false);assert.equal(fixture.calls.at(-1),'closed');
});
test('chooser cancel is recoverable and never leaves the transport busy', async () => {
 const connection=new SerialConnection({requestPort:async()=>{throw Object.assign(new Error('cancel'),{name:'NotFoundError'});}},()=>{});
 await assert.rejects(connection.connect(),/cancel/);assert.equal(connection.state,'disconnected');
 await connection.disconnect();
});
test('device removal and port-open errors return to a reconnectable state', async () => {
 const fixture=fakePort();const connection=new SerialConnection({requestPort:async()=>fixture.port},()=>{});
 await connection.connect();fixture.unplug();await connection.readTask;
 assert.equal(connection.state,'disconnected');assert.equal(fixture.port.readable.locked,false);
 const broken=new SerialConnection({requestPort:async()=>({open:async()=>{throw new Error('busy');}})},()=>{});
 await assert.rejects(broken.connect(),/busy/);assert.equal(broken.state,'disconnected');
});
test('fresh valid telemetry clears an earlier fault after IMUDIAG stops on recovery', () => {
 const model=new DiagnosticModel();model.add('IMUDIAG present=1 fault=1 cal=400/400 invalid=1',1000);
 model.add('fps=40 valid=1 cal=400',2000);
 assert.equal(classify(model,2001,'connected').code,'ready');
});
test('a new calibration window overrides the previous valid performance flag', () => {
 const model=new DiagnosticModel();model.add('fps=40 valid=1 cal=400',1000);model.add(blocked,2000);
 assert.equal(classify(model,2001,'connected').code,'gyro_rate');
});
test('export after disconnect does not describe history as currently connected and healthy', () => {
 const model=new DiagnosticModel();model.add('fps=40 valid=1 cal=400',1000);
 assert.match(makeReport(model,1100,{connection:'disconnected'}),/分析：等待连接/);
});
test('new diagnostic lines authorize a healthy state without a separate performance line', () => {
 const model=new DiagnosticModel();model.add('IMUDIAG v=caldiag2 present=1 fault=0 valid=1 cal=400/400 gyro_dps=0,0,0 g=1 hz=200',1000);
 assert.equal(classify(model,1001,'connected').code,'ready');
});
test('performance updates do not fabricate new gyro samples from a retained value', () => {
 const model=new DiagnosticModel();model.add(blocked,1000);model.add('fps=40 valid=1 cal=400',2000);
 assert.equal(model.history.length,1);assert.equal(model.gyroAt,1000);
});
test('exported example reports stay labelled as example after reimport', () => {
 const model=new DiagnosticModel();model.source='example';model.add(blocked,1000);
 const imported=new DiagnosticModel();imported.importText(makeReport(model,2000),3000);
 assert.equal(imported.source,'example');assert.equal(imported.telemetry.count,0);
 assert.deepEqual(imported.telemetry.gyro,[6.25,-.5,.125]);
});
test('an unrelated imported text file is not described as waiting on an open serial port', () => {
 const model=new DiagnosticModel();model.importText('not an instrument log',1000);
 assert.equal(classify(model,1001,'disconnected').code,'unrecognized');
});
test('a board reboot clears displayed sensor values but retains the raw capture', () => {
 const model=new DiagnosticModel();model.add(blocked,1000);model.add('ESP-ROM:esp32s3-20210327',2000);
 assert.equal(model.telemetry.gyro,null);assert.equal(model.telemetry.gravity,null);
 assert.equal(model.telemetry.count,null);assert.equal(model.logs.length,2);
});

function reusablePort(){
 let controller,opens=0,closes=0;const writes=[];
 const port={readable:null,writable:null,
  async open(){assert.equal(port.readable,null);opens++;port.readable=new ReadableStream({start(c){controller=c;}});port.writable=new WritableStream({write(bytes){writes.push(new TextDecoder().decode(bytes));}});},
  async close(){assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);closes++;port.readable=null;port.writable=null;},getInfo(){return {usbVendorId:0x303a};}};
 return {port,writes,get opens(){return opens;},get closes(){return closes;},send(text){controller.enqueue(new TextEncoder().encode(text));}};
}
test('installation shares the selected port, releases the reader, and resumes diagnostics without a second chooser',async()=>{
 const f=reusablePort(),lines=[];let choices=0;
 const c=new SerialConnection({requestPort:async()=>{choices++;return f.port;}},line=>lines.push(line));
 await c.connect();f.send('before\n');await new Promise(resolve=>setImmediate(resolve));
 await c.install(async port=>{
  assert.equal(port,f.port);assert.equal(c.state,'installing');assert.equal(port.readable.locked,false);
  await assert.rejects(c.calibrate(),/先连接|安装/);
  await assert.rejects(c.disconnect(),/安装/);
  await port.close();await port.open(); // The ESP Web Tools dialog reopens after flashing.
 });
 f.send('after\n');await new Promise(resolve=>setImmediate(resolve));
 assert.equal(choices,1);assert.equal(c.state,'connected');assert.deepEqual(lines,['before','after']);
 await c.calibrate();assert.deepEqual(f.writes,['c']);await c.disconnect();
});
test('cancelled or failed installer restores telemetry, and a closed port is reopened using the same authorization',async()=>{
 const f=reusablePort();let choices=0;const c=new SerialConnection({requestPort:async()=>{choices++;return f.port;}},()=>{});
 await assert.rejects(c.install(async port=>{await port.close();throw new Error('cancelled');}),/cancelled/);
 assert.equal(c.state,'connected');assert.equal(choices,1);assert.ok(f.opens>=2);
 await c.install(async port=>assert.equal(port,f.port));assert.equal(choices,1);
 await new Promise(resolve=>setImmediate(resolve));assert.equal(c.state,'connected');
 await c.disconnect();
});
test('device removed during install leaves a recoverable disconnected session',async()=>{
 const f=reusablePort(),c=new SerialConnection({requestPort:async()=>f.port},()=>{});
 await assert.rejects(c.install(async port=>{await port.close();port.open=async()=>{throw new Error('removed');};}),/removed/);
 assert.equal(c.state,'disconnected');assert.equal(c.port,null);
});

test('extended hardware status parses identity, separate read faults and timestamp with no fabricated pass',()=>{
 const m=new DiagnosticModel();
 m.add('IMUHW who=5 rev=1 ctrl2=21 ctrl3=101 ctrl7=3 temp_c=25.5 nack=2 short=1 sat=3 notready=5 repeats=7 timestamp=1234 restore=1',1000);
 assert.equal(m.hardware.imu.who,5);assert.equal(m.hardware.imu.nack,2);assert.equal(m.hardware.imu.temp_c,25.5);
 assert.equal(m.hardware.selftest,null);
 m.add('SELFTEST id=1 running=0 accel=1 gyro=4 restore=1 accel_mg=250,260,270 gyro_dps=0,0,0',2000);
 assert.equal(m.hardware.selftest.gyro,4);
 const report=makeReport(m,2000,{connection:'connected'});assert.match(report,/IMUHW/);assert.match(report,/硬件/);
});
test('fast IMU samples and hardware-only traffic preserve true sensor freshness',()=>{
 const m=new DiagnosticModel({maxHistory:200});
 for(let i=0;i<100;i++)m.add('IMUFAST seq='+i+' valid=0 accel_g=0,0,-1 gyro_dps=6,0,0 g=1 cal=0/400 reason=gyro_rate',1000+i*50);
 assert.equal(m.history.length,100);assert.equal(m.lastTelemetryAt,5950);
 m.add('BOARD pmu=1 touch=1 expander=1 panel=1 heap=1234 psram=8388608',10000);
 assert.equal(m.lastTelemetryAt,5950);assert.equal(classify(m,10000,'connected').code,'stale');
});
test('hardware summaries distinguish stale, pending, absent battery, timeout and partial memory coverage in Chinese',()=>{
 const m=new DiagnosticModel();
 m.add('BOARD pmu=1 touch=1 expander=1 panel=1 battery=0 vbus_mv=4990 sys_mv=3900 heap=123456 psram=8388608',1000);
 m.add('IMUHW who=5 rev=1 ctrl2=21 ctrl3=101 ctrl7=3 temp_c=25 nack=0 short=0 sat=0 notready=0 repeats=0 timestamp=5 restore=1',1000);
 m.add('SELFTEST id=1 running=0 accel=1 gyro=4 restore=1 accel_mg=250,250,250 gyro_dps=0,0,0',1000);
 const rows=require('../installer/diagnostics-core.js').hardwareChecks(m,1100,'connected');
 assert.match(rows.find(r=>r.id==='selftest').description,/超时/);
 assert.doesNotMatch(rows.find(r=>r.id==='power').description,/电池.*故障/);
 assert.equal(rows.find(r=>r.id==='memory').tone,'neutral');
 const old=require('../installer/diagnostics-core.js').hardwareChecks(m,7000,'connected');
 assert.equal(old.find(r=>r.id==='identity').tone,'neutral');
});
test('partial hardware messages and self-test data never imply unperformed tests passed',()=>{
 const {hardwareChecks}=require('../installer/diagnostics-core.js'),m=new DiagnosticModel();
 m.add('IMUHW who=5 temp_c=NaN',1000);m.add('BOARD pmu=1 heap=42',1000);
 const checks=hardwareChecks(m,1001,'connected');
 for(const id of ['bus','temperature','system','power'])assert.notEqual(checks.find(r=>r.id===id).tone,'success');
 m.add('MEMORY id=1 pending=1 ram=-1 psram=-1 flash=-1',1000);
 assert.equal(hardwareChecks(m,1001,'connected').find(r=>r.id==='memory').tone,'neutral');
});
test('duplicate high-rate sequence numbers do not fabricate sensor history',()=>{
 const m=new DiagnosticModel();for(let i=0;i<20;i++)m.add('IMUFAST seq=5 valid=0 gyro_dps=0,0,0 g=1 cal=0/400',1000+i*50);
 assert.equal(m.history.length,1);assert.equal(m.gyroAt,1000);
});
test('stationary statistics identify a constant high raw bias despite low noise and clear on reconnect',()=>{
 const {stationarySummary}=require('../installer/diagnostics-core.js'),m=new DiagnosticModel({maxHistory:220});
 for(let i=0;i<200;i++)m.add('IMUFAST seq='+i+' valid=0 gyro_dps=11.25,0,0 g=1 cal=0/400',1000+i*50);
 const r=stationarySummary(m,1000,11000);assert.equal(r.tone,'warning');assert.match(r.text,/零偏/);assert.equal(r.std[0],0);
 m.stationaryResult=r;m.resetTelemetry();assert.equal(m.stationaryResult,null);
});
test('stationary summary uses the 10 deg/s combined ceiling for reported bias and boundary samples',()=>{
 const {stationarySummary}=require('../installer/diagnostics-core.js');
 for(const [gyro,want] of [['-0.21,3.45,-4.01','success'],['9.99,0,0','success'],
                          ['10.01,0,0','warning'],['6,6,6','warning']]){
  const m=new DiagnosticModel();
  for(let i=0;i<20;i++)m.add(`IMUFAST seq=${i} valid=0 gyro_dps=${gyro} g=1 cal=0/400`,1000+i*50);
  assert.equal(stationarySummary(m,1000,2000).tone,want,gyro);
 }
});
test('isolated historical timestamp repeats are not a freeze, but a timestamp stuck across reports is flagged',()=>{
 const {hardwareChecks}=require('../installer/diagnostics-core.js'),m=new DiagnosticModel();
 const row='IMUHW who=5 rev=124 ctrl2=21 ctrl3=101 ctrl7=3 nack=0 short=0 sat=0 notready=10 repeats=2 timestamp=42';
 m.add(row,1000);assert.equal(hardwareChecks(m,1001,'connected').find(r=>r.id==='bus').tone,'success');
 m.add(row,2000);m.add(row,3000);
 assert.match(hardwareChecks(m,3001,'connected').find(r=>r.id==='bus').description,/冻结/);
 m.add(row.replace('timestamp=42','timestamp=43'),4000);
 assert.equal(hardwareChecks(m,4001,'connected').find(r=>r.id==='bus').tone,'success');
});
test('incomplete completion flags never authorize a self-test or memory pass',()=>{
 const {hardwareChecks}=require('../installer/diagnostics-core.js'),m=new DiagnosticModel();
 m.add('SELFTEST id=1 accel=1 gyro=1 restore=1',1000);
 m.add('MEMORY id=1 ram=1 psram=1 flash=1',1000);
 for(const id of ['selftest','memory'])assert.notEqual(hardwareChecks(m,1001,'connected').find(r=>r.id===id).tone,'success');
});
test('RTC, audio interfaces and absent SD card have separate Chinese results',()=>{
 const {hardwareChecks}=require('../installer/diagnostics-core.js'),m=new DiagnosticModel();
 m.add('PERIPHERAL rtc=1 rtc_sec=12 rtc_os=1 rtc_stop=0 rtc_tick=1 dac=0 mic=1',1000);
 m.add('SDCHECK id=1 pending=0 result=2 bytes=0',1000);
 const rows=hardwareChecks(m,1001,'connected');
 assert.match(rows.find(r=>r.id==='rtc').description,/时间|失效/);
 assert.equal(rows.find(r=>r.id==='audio').tone,'neutral');
 assert.equal(rows.find(r=>r.id==='sd').tone,'neutral');
 assert.match(rows.find(r=>r.id==='sd').description,/未插卡/);
});
test('export file retains real capture and uses a readable safe Chinese filename',()=>{
 const {reportFile}=require('../installer/diagnostics-core.js'),m=new DiagnosticModel();m.add(blocked,1000);
 const file=reportFile(m,2000,{connection:'disconnected'});
 assert.match(file.filename,/^仪表诊断_设备实测_.*\.txt$/);assert.doesNotMatch(file.filename,/[/:*?<>|]/);
 assert.match(file.content,/IMUDIAG/);assert.equal(file.rows,1);
 m.source='example';assert.match(reportFile(m,2000,{}).filename,/示例/);
});
