#pragma once
namespace esphome::odelic_setup {
static const char PAGE[] = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Odelic light setup</title><style>
body{font:17px system-ui;max-width:720px;margin:32px auto;padding:0 20px;color:#172638;background:#f6f8fb}
section{background:white;padding:20px;border-radius:12px;margin:18px 0}label{display:block;margin:12px 0}
input{font:inherit;padding:8px;max-width:90%;box-sizing:border-box}button{font:inherit;padding:10px 16px;margin:6px 4px 6px 0;cursor:pointer}
#status{white-space:pre-wrap}small{display:block}li{margin:12px 0;overflow-wrap:anywhere}.muted{color:#526377}
</style><h1>Your Odelic lights</h1>
<p>Add lights now or later, without reflashing. Changes restart the bridge and briefly disconnect all its lights.</p>
<p id="status" role="status" aria-live="polite">Loading…</p>
<section><h2>Saved lights</h2><p id="capacity"></p><ul id="lights"></ul>
<button id="add" type="button">Add a light</button></section>
<section><h2 id="formtitle">Add a light</h2><form id="details">
<label>Light name<br><input id="name" placeholder="Living room" maxlength="64" required autocomplete="off"></label>
<label>Light Bluetooth MAC address<br><input id="mac" placeholder="10:20:30:40:50:60" maxlength="17" required autocomplete="off"></label>
<label>Eight-digit lighting ID<br><input id="lighting" type="password" inputmode="numeric" pattern="[0-9]{8}" minlength="8" maxlength="8" autocomplete="off"></label>
<small id="idhelp">Find the lighting ID in ConnectedLighting. Keep any leading zeroes.</small>
<label><input type="checkbox" id="ownership" required> I confirm this MAC belongs to my light.</label>
<button id="save">Save and restart bridge</button></form>
<p>After restarting, the bridge checks each saved light. Saving does not change the fixture's power, brightness, colour, or remote pairing.</p></section>
<section><h2>Find my light</h2><p>Close ConnectedLighting. Keep every other light's wall switch unchanged. Use one ordinary OFF/ON cycle with time to scan; do not rapidly cycle power.</p>
<ol><li>Leave the target's wall switch ON. Start the guide.</li><li>When asked, turn only that wall switch OFF, wait, then capture OFF.</li><li>When asked, turn it back ON, wait, then capture restored ON.</li><li>Use the matching MAC in the form above, confirm ownership, and save.</li></ol>
<button id="start" type="button">Start with wall power ON</button>
<button id="off" type="button" hidden>Wall power is OFF — scan</button>
<button id="restored" type="button" hidden>Wall power is ON again — scan</button>
<button id="cancel" type="button" hidden>Cancel guide</button>
<ul id="results"></ul><p>The advertised name is only a hint. Neighbours may have identical lights. Repeat an ambiguous result; don't guess.</p>
<p>Each stage takes two fresh five-second scans. Control of all saved lights is paused during the guide. Always restore wall power ON before finishing or cancelling. The guide times out after five minutes.</p></section>
<script>
let token='',latest=null,uid=0,revision=0,initialized=false;
const $=id=>document.getElementById(id);
$('mac').addEventListener('input',()=>{$('ownership').checked=false});
$('name').addEventListener('input',()=>{$('name').setCustomValidity(new TextEncoder().encode($('name').value).length>64?'Name is too long; please shorten it.':'')});
function edit(light){
 uid=light?light.uid:0;revision=latest.revision;
 $('name').value=light?light.name:'';$('mac').value=light?light.mac:'';$('lighting').value='';$('ownership').checked=false;
 $('formtitle').textContent=light?'Edit '+light.name:'Add a light';$('name').setCustomValidity('');
 $('lighting').required=!uid;$('idhelp').textContent=uid?'Leave ID blank to keep this light’s saved ID, or enter a new eight-digit ID.':'Find the eight-digit lighting ID in ConnectedLighting. Keep leading zeroes.';
 render();$('name').focus();
}
function render(){
 const s=latest,blocked=s.busy||s.wizard;
 $('capacity').textContent=s.lights.length+' of '+s.capacity+' lights saved';
 $('add').disabled=blocked||s.locked||s.lights.length>=s.capacity;
 $('save').disabled=blocked||s.locked||(!uid&&s.lights.length>=s.capacity);
 $('off').hidden=s.phase!==1||s.busy;$('restored').hidden=s.phase!==2||s.busy;
 $('cancel').hidden=!s.wizard;$('start').disabled=blocked;
 $('lights').replaceChildren();
 for(const light of s.lights){
  const li=document.createElement('li'),text=document.createElement('div');text.textContent=light.name+' — '+light.mac;li.appendChild(text);
  const e=document.createElement('button');e.type='button';e.textContent='Edit';e.disabled=blocked;e.onclick=()=>edit(light);li.appendChild(e);
  const r=document.createElement('button');r.type='button';r.textContent='Remove';r.disabled=blocked;
  r.onclick=()=>{if(confirm('Remove '+light.name+' from this bridge? The light and remote pairing will not be reset.'))action('remove',{uid:light.uid,revision:s.revision})};li.appendChild(r);$('lights').appendChild(li);
 }
 $('results').replaceChildren();
 for(const c of s.candidates){
  const li=document.createElement('li');li.textContent=c.mac+' '+c.name+' (RSSI '+c.rssi+') ';
  if(s.candidates.length===1){const b=document.createElement('button');b.type='button';b.textContent='Use this MAC';b.onclick=()=>{$('mac').value=c.mac;$('ownership').checked=false;$('mac').focus()};li.appendChild(b)}
  $('results').appendChild(li);
 }
}
async function refresh(){
 try{
  const r=await fetch('/odelic/setup/state',{cache:'no-store'});if(!r.ok)throw Error('Could not read status');
  latest=await r.json();token=latest.token;$('status').textContent=latest.message;
  if(!initialized){initialized=true;revision=latest.revision;$('lighting').required=true}
  render();
 }catch(e){$('status').textContent='Bridge unavailable. If restarting, wait and reload this page.'}
}
async function action(action,extra={}){
 try{
  const body=new URLSearchParams({action,token,...extra});
  const r=await fetch('/odelic/setup/action',{method:'POST',body});if(!r.ok)throw Error(await r.text());
  if(action==='save')$('lighting').value='';await refresh();
 }catch(e){$('status').textContent=e.message}
}
$('add').onclick=()=>edit(null);
$('details').onsubmit=e=>{e.preventDefault();action('save',{uid,revision,name:$('name').value,mac:$('mac').value,lighting_id:$('lighting').value,confirmed:'yes'})};
for(const name of ['start','off','restored','cancel'])$(name).onclick=()=>action(name);
refresh();setInterval(refresh,1500);
</script></html>)HTML";
}
