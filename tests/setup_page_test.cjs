// Actual firmware HTML with simulated HTTP responses; no physical device access.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
(async () => {
  const source = fs.readFileSync(path.join(__dirname, '../components/odelic_setup/page.h'), 'utf8');
  const html = source.match(/R"HTML\(([\s\S]*)\)HTML"/)[1];
  const browser = await chromium.launch({headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH});
  try {
    const page = await browser.newPage({viewport: {width: 900, height: 1100}});
    let state = {token: 'synthetic-csrf', wizard: false, busy: false, phase: 0, capacity: 8, revision: 0, lights: [], candidates: [], message: 'No saved lights.'};
    let nextUid = 1;
    const actions = [];
    await page.route('http://odelic.test/**', async route => {
      const request = route.request();
      if (request.url().endsWith('/state')) return route.fulfill({json: state});
      if (request.url().endsWith('/action')) {
        assert.equal(request.method(), 'POST');
        const values = Object.fromEntries(new URLSearchParams(request.postData()));
        assert.equal(values.token, state.token);
        actions.push(values);
        if (values.action === 'save' || values.action === 'remove') {
          assert.equal(Number(values.revision), state.revision, 'UI must submit the revision it edited');
          if (values.action === 'save') {
            const uid = Number(values.uid) || nextUid++;
            state.lights = state.lights.filter(light => light.uid !== uid);
            state.lights.push({uid, name: values.name, mac: values.mac});
          } else state.lights = state.lights.filter(light => light.uid !== Number(values.uid));
          state.revision++;state.busy=true;state.message='Light list saved. Restarting bridge.';
        } else if (values.action === 'start') {
          state = {...state, wizard: true, phase: 1, candidates: [], message: 'Turn target wall power OFF.'};
        } else if (values.action === 'off') {
          state = {...state, phase: 2, message: 'Restore target wall power ON.'};
        } else if (values.action === 'restored') {
          state = {...state, wizard: false, phase: 3, candidates: [{mac: '10:20:30:40:50:70', name: '<img src=x onerror=alert(1)>', rssi: -71}], message: 'One consistent candidate found.'};
        } else if (values.action === 'cancel') state = {...state, wizard: false, message: 'Guide cancelled. Restore wall power ON.'};
        return route.fulfill({status: 202, body: 'Accepted'});
      }
      return route.fulfill({contentType: 'text/html', body: html});
    });
    const reboot = async () => {state.busy=false;state.message='Saved lights loaded.';await page.reload();await page.waitForFunction(() => document.querySelector('#status').textContent.includes('Saved lights loaded'))};
    await page.goto('http://odelic.test/odelic/setup');
    await page.waitForFunction(() => document.querySelector('#status').textContent.includes('No saved'));
    assert.equal(await page.locator('#lighting').getAttribute('type'), 'password');
    for(let i=0;i<3;i++){
      await page.locator('#add').click();
      await page.locator('#name').fill(['Living room','Kitchen','Bedroom'][i]);
      await page.locator('#mac').fill('10:20:30:40:50:6'+i);
      await page.locator('#lighting').fill('00010007');
      const before=actions.length;
      await page.locator('#save').click();assert.equal(actions.length,before,'ownership confirmation is required');
      await page.locator('#ownership').check();await page.locator('#save').click();
      await page.waitForFunction(() => document.querySelector('#status').textContent.includes('Restarting'));
      assert.equal(await page.locator('#lighting').inputValue(),'');
      await reboot();
    }
    assert.equal(await page.locator('#lights li').count(),3);
    await page.locator('#lights li').filter({hasText:'Kitchen'}).getByRole('button',{name:'Edit',exact:true}).click();
    await page.locator('#name').fill('台所');
    assert.equal(await page.locator('#lighting').evaluate(el=>el.required),false);
    await page.locator('#ownership').check();await page.locator('#save').click();
    await page.waitForFunction(() => document.querySelector('#status').textContent.includes('Restarting'));
    assert.equal(actions.at(-1).uid,'2');assert.equal(actions.at(-1).lighting_id,'');await reboot();
    page.once('dialog',dialog=>dialog.accept());
    await page.locator('#lights li').filter({hasText:'Bedroom'}).getByRole('button',{name:'Remove',exact:true}).click();
    await page.waitForFunction(() => document.querySelector('#status').textContent.includes('Restarting'));await reboot();
    assert.equal(await page.locator('#lights li').count(),2);
    await page.locator('#add').click();await page.locator('#start').click();
    await page.locator('#off').waitFor({state:'visible'});assert.equal(await page.locator('#save').isDisabled(),true);
    await page.locator('#off').click();await page.locator('#restored').waitFor({state:'visible'});await page.locator('#restored').click();
    await page.getByRole('button',{name:'Use this MAC'}).click();
    assert.equal(await page.locator('#mac').inputValue(),'10:20:30:40:50:70');
    assert.equal(await page.locator('#ownership').isChecked(),false);
    assert.equal(await page.locator('#results img').count(),0,'advertised names are inert text');
    state.candidates.push({mac:'10:20:30:40:50:72',name:'same name',rssi:-40});
    await page.waitForFunction(()=>document.querySelectorAll('#results li').length===2);
    assert.equal(await page.getByRole('button',{name:'Use this MAC'}).count(),0);
    while(state.lights.length<8)state.lights.push({uid:nextUid++,name:'Extra light '+nextUid,mac:'10:20:30:40:50:80'});
    await page.waitForFunction(()=>document.querySelector('#add').disabled);
    assert.equal(await page.locator('#save').isDisabled(),true);
    await page.locator('#lights li').first().getByRole('button',{name:'Edit',exact:true}).click();
    assert.equal(await page.locator('#save').isDisabled(),false,'full capacity still permits editing');
    await page.setViewportSize({width:390,height:844});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),true);
    await page.screenshot({path:'/tmp/odelic-setup-page.png',fullPage:true});
    console.log('Setup page passed: three lights, editing, confirmed removal, ID privacy, guided scans, ambiguity, capacity and phone layout.');
  } finally {await browser.close()}
})().catch(error=>{console.error(error);process.exitCode=1});
