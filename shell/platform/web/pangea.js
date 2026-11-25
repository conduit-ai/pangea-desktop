/* Its Felt parts derive from Pattern Player by Lukasz Furman: licensed as shell/platform/web/FELT-LICENSE.md says. Also offered under AGPL-3.0-or-later, at your choice (25 November 2026). */
/* Pangea UI library — pangea.js. Vanilla JS, no dependencies, nothing loaded from the network.
   Pangea.render(el, spec, params) draws a plugin from a spec. Every control is built by Pangea.control(type, param, opts). */
(function(){
'use strict';
const P=window.Pangea=window.Pangea||{};
const h=(c,html,tag)=>{const e=document.createElement(tag||'div');if(c)e.className=c;if(html!=null)e.innerHTML=html;return e};
const clamp=(x,a,b)=>Math.max(a==null?0:a,Math.min(b==null?1:b,x));
const esc=s=>String(s).replace(/[&<>"]/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[m]));
const add=(el,...kids)=>{kids.forEach(k=>k&&el.append(k));return el};

/* ---------- colour: the five palette colours in, guarded derived tokens out ---------- */
const hex=c=>{c=c.replace('#','');if(c.length===3)c=c.replace(/./g,'$&$&');return [0,2,4].map(i=>parseInt(c.substr(i,2),16))};
const toHex=a=>'#'+a.map(x=>Math.round(clamp(x,0,255)).toString(16).padStart(2,'0')).join('');
const lum=a=>{const f=x=>{x/=255;return x<=.03928?x/12.92:Math.pow((x+.055)/1.055,2.4)};return .2126*f(a[0])+.7152*f(a[1])+.0722*f(a[2])};
const cr=(a,b)=>{const x=lum(a),y=lum(b);return (Math.max(x,y)+.05)/(Math.min(x,y)+.05)};
const mix=(a,b,t)=>a.map((x,i)=>x+(b[i]-x)*t);
const W=[255,255,255],K=[0,0,0];
const pole=b=>cr(W,b)>=cr(K,b)?W:K;
const ensure=(f,b,r)=>{if(cr(f,b)>=r)return f;const p=pole(b);for(let t=.08;t<1;t+=.08){const c=mix(f,p,t);if(cr(c,b)>=r)return c}return p};
const MATSHIFT={matte:[.06,.08],brushed_metal:[.12,.14],painted_metal:[.07,.12],bakelite:[.08,.2],wood:[.1,.16],paper:[.08,.08],/* Pangea patch: felt, the Felt look's material (docs/felt-look.md) */felt:[.03,.04]};
P.palettes={dark:{bg:'#131417',panel:'#24262c',text:'#e8e6e1',accent:'#ff6b3d',highlight:'#ffd166'},light:{bg:'#d9d4c7',panel:'#f1ede3',text:'#1d1c1a',accent:'#2456e6',highlight:'#e0452b'},loud:{bg:'#ff2bd6',panel:'#00d8f0',text:'#fff200',accent:'#ff5a00',highlight:'#6a00ff'}};
P.theme=function(el,pal,material,variant){
  if(typeof pal==='string')pal=P.palettes[pal]||(pal.indexOf('#')===0?(a=>({bg:a[0],panel:a[1],text:a[2],accent:a[3],highlight:a[4]}))(pal.split(',')):P.palettes.dark);
  const c={};['bg','panel','text','accent','highlight'].forEach(k=>{c[k]=hex(pal[k]);el.style.setProperty('--'+k,pal[k])});
  const sh=MATSHIFT[material]||MATSHIFT.matte,light=variant==='light';
  const face=mix(c.panel,light?W:K,sh[light?0:1]);
  const inkF=ensure(c.text,face,4.5),inkB=ensure(c.text,c.bg,4.5);
  const screen=mix(c.bg,pole(inkB),.3),inkS=ensure(c.text,screen,4.5);
  const best=t=>[c.text,c.bg,c.panel].sort((a,b)=>cr(b,t)-cr(a,t))[0];
  const onA=ensure(best(c.accent),c.accent,4.5),onH=ensure(best(c.highlight),c.highlight,4.5);
  const set={'face-c':face,'ink-face':inkF,'ink-bg':inkB,'ink-screen':inkS,'screen-c':screen,
    'acc-face':ensure(c.accent,face,2),'acc-bg':ensure(c.accent,c.bg,2),'hl-face':ensure(c.highlight,face,2),'hl-bg':ensure(c.highlight,c.bg,2),
    'trace':ensure(c.accent,screen,2.6),'trace-2':ensure(c.highlight,screen,2.6),'on-acc':onA,'on-hl':onH,
    'away-face':pole(inkF),'away-bg':pole(inkB),'away-screen':pole(inkS),'away-acc':pole(onA)};
  for(const k in set)el.style.setProperty('--'+k,toHex(set[k]));
  return set;
};

/* ---------- fonts: [group, title family, ui family, title weight, label weight, label case, label tracking, size factor, title case, title tracking, licence, files] ---------- */
const F=(g,t,u,tw,lw,cs,trk,fs,tc,tt,files)=>({group:g,title:t,ui:u,tw,lw,case:cs,trk,fs,tcase:tc,ttrk:tt,licence:'SIL OFL 1.1',files});
P.fonts={
 grotesk_1:F('grotesk','Space Grotesk','Space Grotesk',700,500,'uppercase','.07em',1,'none','-.02em',['SpaceGrotesk[wght].woff2']),
 grotesk_2:F('grotesk','Instrument Sans','Instrument Sans',700,500,'uppercase','.07em',1,'none','-.02em',['InstrumentSans[wdth,wght].woff2']),
 grotesk_3:F('grotesk','Hanken Grotesk','Hanken Grotesk',800,500,'none','.01em',1.04,'none','-.02em',['HankenGrotesk[wght].woff2']),
 grotesk_4:F('grotesk','Familjen Grotesk','Familjen Grotesk',700,500,'uppercase','.07em',1.02,'none','-.01em',['FamiljenGrotesk[wght].woff2']),
 condensed_1:F('condensed','Barlow Condensed','Barlow Condensed',700,600,'uppercase','.09em',1.14,'uppercase','.02em',['BarlowCondensed-Medium.woff2','BarlowCondensed-SemiBold.woff2','BarlowCondensed-Bold.woff2']),
 condensed_2:F('condensed','Oswald','Oswald',600,500,'uppercase','.08em',1.06,'uppercase','.01em',['Oswald[wght].woff2']),
 condensed_3:F('condensed','Archivo Narrow','Archivo Narrow',700,600,'uppercase','.07em',1.06,'uppercase','.02em',['ArchivoNarrow[wght].woff2']),
 condensed_4:F('condensed','Saira Condensed','Saira Condensed',700,600,'uppercase','.08em',1.1,'uppercase','.03em',['SairaCondensed-Medium.woff2','SairaCondensed-SemiBold.woff2','SairaCondensed-Bold.woff2']),
 mono_1:F('mono','JetBrains Mono','JetBrains Mono',700,500,'uppercase','.02em',.94,'uppercase','0',['JetBrainsMono[wght].woff2']),
 mono_2:F('mono','IBM Plex Mono','IBM Plex Mono',600,500,'none','0',.96,'none','-.02em',['IBMPlexMono-Regular.woff2','IBMPlexMono-Medium.woff2','IBMPlexMono-SemiBold.woff2']),
 mono_3:F('mono','Space Mono','Space Mono',700,400,'uppercase','0',.92,'none','-.03em',['SpaceMono-Regular.woff2','SpaceMono-Bold.woff2']),
 mono_4:F('mono','DM Mono','DM Mono',500,500,'uppercase','.03em',.96,'none','-.02em',['DMMono-Regular.woff2','DMMono-Medium.woff2']),
 serif_1:F('serif','Newsreader','Newsreader',600,500,'none','.01em',1.1,'none','-.01em',['Newsreader[opsz,wght].woff2']),
 serif_2:F('serif','Young Serif','Young Serif',400,400,'none','.01em',1.02,'none','-.01em',['YoungSerif-Regular.woff2']),
 serif_3:F('serif','Zilla Slab','Zilla Slab',700,600,'uppercase','.06em',1.08,'none','-.01em',['ZillaSlab-Medium.woff2','ZillaSlab-SemiBold.woff2','ZillaSlab-Bold.woff2']),
 serif_4:F('serif','DM Serif Display','DM Serif Text',400,400,'none','.01em',1.08,'none','0',['DMSerifDisplay-Regular.woff2','DMSerifText-Regular.woff2']),
 display_1:F('display','Bungee','Space Grotesk',400,600,'uppercase','.07em',1,'uppercase','0',['Bungee-Regular.woff2','SpaceGrotesk[wght].woff2']),
 display_2:F('display','Unbounded','Hanken Grotesk',800,600,'uppercase','.06em',1.02,'uppercase','-.02em',['Unbounded[wght].woff2','HankenGrotesk[wght].woff2']),
 display_3:F('display','Syne','Syne',800,600,'uppercase','.06em',1,'none','-.03em',['Syne[wght].woff2']),
 display_4:F('display','Dela Gothic One','Instrument Sans',400,600,'uppercase','.07em',1,'none','-.01em',['DelaGothicOne-Regular.woff2','InstrumentSans[wdth,wght].woff2']),
 /* Pangea patch: Jost, the Felt look's font (docs/felt-look.md): a Light title in spaced capitals and Medium labels in spaced capitals, as Pattern Player draws them; Jost is small on its body, so it is drawn a little larger. */
 jost:F('grotesk','Jost','Jost',300,500,'uppercase','.14em',1.12,'uppercase','.3em',['Jost-300-latin.woff2','Jost-400-latin.woff2','Jost-500-latin.woff2'])
};
P.font=function(el,id){const f=P.fonts[id]||P.fonts.grotesk_1,s=el.style,fb=f.group==='mono'?'ui-monospace,Menlo,monospace':f.group==='serif'?'Georgia,serif':'system-ui,sans-serif';
  s.setProperty('--f-title',"'"+f.title+"',"+fb);s.setProperty('--f-ui',"'"+f.ui+"',"+(f.group==='display'?'system-ui,sans-serif':fb));
  s.setProperty('--tw',f.tw);s.setProperty('--lw',f.lw);s.setProperty('--case',f.case);s.setProperty('--trk',f.trk);s.setProperty('--fs',f.fs);s.setProperty('--t-case',f.tcase);s.setProperty('--t-trk',f.ttrk)};

/* ---------- parameters ---------- */
function param(d){const p=Object.assign({kind:'continuous',min:0,max:1,unit:''},typeof d==='string'?{id:d}:d);
  if(p.kind==='choice'){p.min=0;p.max=p.choices.length-1}
  if(p.kind==='toggle'||p.kind==='momentary'){p.min=0;p.max=1}
  if(p.default==null)p.default=p.kind==='continuous'?(p.scale==='log'?Math.sqrt(p.min*p.max):(p.min+p.max)/2):p.min;
  if(p.label==null){p.label=p.id;if(p.unit&&p.label.endsWith(' '+p.unit))p.label=p.label.slice(0,-p.unit.length-1)}
  p.steps=p.kind==='continuous'?0:Math.round(p.max-p.min)+1;return p}
const toVal=(p,n)=>{const v=p.scale==='log'?p.min*Math.pow(p.max/p.min,n):p.min+(p.max-p.min)*n;return p.steps?Math.round(v):v};
const toN=(p,v)=>clamp(p.scale==='log'?Math.log(v/p.min)/Math.log(p.max/p.min):(v-p.min)/((p.max-p.min)||1));
const snap=(p,n)=>p.steps>1?Math.round(n*(p.steps-1))/(p.steps-1):n;
function fmt(p,x){const u=p.unit||'';
  if(p.kind==='toggle'||p.kind==='momentary')return x>=.5?'On':'Off';
  if(p.kind==='choice')return p.choices[Math.round(x)]||'';
  if(u==='Hz')return x>=1000?(x/1000).toFixed(x>=10000?1:2)+' kHz':x.toFixed(x<100?1:0)+' Hz';
  if(u==='ms')return x>=1000?(x/1000).toFixed(2)+' s':x.toFixed(x<10?1:0)+' ms';
  if(u==='s')return x<1?Math.round(x*1000)+' ms':x.toFixed(2)+' s';
  if(u==='dB')return (x>0&&p.min<0?'+':'')+x.toFixed(1)+' dB';
  if(u==='%')return Math.round(x)+' %';
  if(u==='st')return (x>0?'+':'')+(p.steps?Math.round(x):x.toFixed(1))+' st';
  if(p.steps)return Math.round(x)+(u?' '+u:'');
  return (p.bipolar&&x>0?'+':'')+x.toFixed(2)+(u?' '+u:'')}
P.param=param;P.fmt=fmt;

/* ---------- footprints in units (multiples of the 8-unit grid): [width, height] ---------- */
const SZ=P.sizes={knob:{s:[64,72],m:[88,96],l:[112,120]},hero:{s:[128,136],m:[184,192],l:[264,272]},
 fader_v:{s:[56,128],m:[64,176],l:[72,240]},fader_h:{s:[120,64],m:[168,64],l:[232,64]},
 toggle:{s:[56,72],m:[64,80],l:[80,88]},rocker:{s:[56,72],m:[64,80],l:[80,88]},lit_button:{s:[56,72],m:[64,80],l:[80,88]},
 segmented:{s:[120,56],m:[168,64],l:[232,72]},dropdown:{s:[104,56],m:[136,64],l:[176,72]},stepped_rotary:{s:[96,104],m:[120,128],l:[152,160]},
 number_box:{s:[64,56],m:[80,64],l:[104,72]},tempo_sync:{s:[152,56],m:[184,64],l:[232,72]},
 vu_needle:{s:[128,104],m:[176,128],l:[240,168]},led_ladder:{s:[48,128],m:[56,176],l:[64,240]},flat_bar:{s:[40,128],m:[48,176],l:[56,240]},gain_reduction:{s:[56,128],m:[64,176],l:[72,240]},
 display:{s:[232,128],m:[360,176],l:[560,240]},xy_pad:{s:[144,144],m:[192,192],l:[264,264]},macro:{s:[112,120],m:[168,152],l:[184,192]},
 /* Pangea patch: the Felt look's parts (docs/felt-look.md): a step bar, the variation's Generate, the chord map and the notes. */
 bar:{s:[32,120],m:[40,160],l:[48,208]},generate:{s:[240,56],m:[288,64],l:[336,72]},chord_map:{s:[176,200],m:[216,240],l:[264,288]},note_roll:{s:[360,120],m:[360,152],l:[560,200]}};
const fix=el=>{if(el._fp){el.style.width='calc('+el._fp[0]+'*var(--u))';el.style.height='calc('+el._fp[1]+'*var(--u))'}return el};

/* ---------- live ticker: one rAF loop for every meter / display, only while on screen ---------- */
const live=new Set();let raf=0;P.animate=true;
const io=typeof IntersectionObserver!=='undefined'?new IntersectionObserver(es=>{es.forEach(e=>{e.target._vis=e.isIntersecting});kick()}):null;
function addLive(el){el._vis=!io;live.add(el);if(io)io.observe(el);kick()}
function kick(){if(!raf&&P.animate)raf=requestAnimationFrame(loop)}
function loop(t){raf=0;let any=false;live.forEach(el=>{if(!el.isConnected){el._dead=(el._dead||0)+1;if(el._dead>600){live.delete(el);if(io)io.unobserve(el)}else any=true;return}el._dead=0;if(el._vis){el.frame(t/1000);any=true}});if(any&&P.animate)raf=requestAnimationFrame(loop)}
P.setAnimate=b=>{P.animate=b;kick()};
const sig=(t,s)=>clamp(.56+.24*Math.sin(t*2.1+s)+.14*Math.sin(t*5.3+s*2)+.07*Math.sin(t*13.7+s));

/* ---------- control shell: label, readout chip, drag, fine adjust, type-a-value ---------- */
function base(cls,p,o,hitCls){const el=h('pg-ctl '+cls);if(o.aria)el.setAttribute('aria-label',o.aria)/* Pangea patch: a name for a screen reader where the label alone says too little (a step's number) */;el.dataset.size=o.size||'m';if(o.look)el.dataset.look=o.look;if(o.state&&o.state!=='edit')el.dataset.state=o.state;el.tabIndex=o.state==='disabled'?-1:0;
  el._p=p;el._n=o.n!=null?o.n:toN(p,p.default);el._hit=h('pg-hit '+(hitCls||''));el._chip=h('pg-chip');el._lbl=h('pg-lbl','<span>'+esc(o.label||p.label)+'</span>');return el}
function finish(el,o){const p=el._p,chip=el._chip;el._hit.append(chip);el.append(el._hit,el._lbl);
  el.apply=()=>{const n=el._n,s=el.style,c=p.bipolar?.5:0;s.setProperty('--v',n);s.setProperty('--a0',Math.min(n,c));s.setProperty('--a1',Math.abs(n-c));
    el.classList.toggle('is-on',p.steps===2&&n>=.5);if(!chip.classList.contains('is-edit'))chip.textContent=fmt(p,toVal(p,n));if(el._ext)el._ext(n)};
  el.setN=(n,silent)=>{n=snap(p,clamp(n));if(n===el._n)return;el._n=n;el.apply();if(!silent&&el.onInput)el.onInput(n)};
  el.apply();if(o.state==='edit')edit(el,true);return el}
function drag(el,axis,travel){const hit=el._hit,p=el._p;let st=null;
  hit.addEventListener('pointerdown',e=>{if(e.button||e.target.closest('.pg-chip.is-edit,.pg-menu,[data-d],[data-m],.pg-ts-s'))return;if(e.altKey){el.setN(toN(p,p.default));return}
    st={x:e.clientX,y:e.clientY,n:el._n};try{hit.setPointerCapture(e.pointerId)}catch(_){}el.classList.add('is-drag')});
  hit.addEventListener('pointermove',e=>{if(!st)return;const fine=e.shiftKey;el.classList.toggle('is-fine',fine);const d=axis==='x'?e.clientX-st.x:st.y-e.clientY,t=typeof travel==='function'?travel():travel;
    st.n=clamp(st.n+d/(t*(fine?8:1)));st.x=e.clientX;st.y=e.clientY;el.setN(st.n)});
  const end=()=>{st=null;el.classList.remove('is-drag','is-fine')};hit.addEventListener('pointerup',end);hit.addEventListener('pointercancel',end);
  hit.addEventListener('dblclick',e=>{if(!e.target.closest('[data-d],[data-m],.pg-ts-s'))edit(el)});
  el.addEventListener('keydown',e=>{const k={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1}[e.key];if(!k)return;e.preventDefault();el.setN(el._n+k*(p.steps>1?1/(p.steps-1):e.shiftKey?.0025:.02))})}
function edit(el,stat){const p=el._p,chip=el._chip,v=toVal(p,el._n),u=p.kind==='choice'?'':p.unit||'';if(chip.classList.contains('is-edit'))return;chip.classList.add('is-edit');
  chip.innerHTML='<input class="pg-in" value="'+(p.kind==='choice'?v+1:Math.round(v*100)/100)+'"'+(stat?' readonly tabindex="-1"':'')+'>'+(u?'<em>'+esc(u)+'</em>':'');
  if(stat)return;const inp=chip.firstChild;inp.focus();inp.select();
  const done=ok=>{if(!chip.classList.contains('is-edit'))return;chip.classList.remove('is-edit');if(ok){let x=parseFloat(inp.value);if(!isNaN(x)){if(p.kind==='choice')x-=1;el.setN(toN(p,clamp(x,p.min,p.max)))}}el.apply()};
  inp.addEventListener('keydown',e=>{e.stopPropagation();if(e.key==='Enter')done(true);if(e.key==='Escape')done(false)});inp.addEventListener('blur',()=>done(true));
  ['pointerdown','dblclick'].forEach(ev=>inp.addEventListener(ev,e=>e.stopPropagation()))}

/* ---------- knobs: flat_ring, metal_cap, bakelite_cap, chicken_head, hero · normal, bipolar, stepped ---------- */
const CH='<svg class="pg-ch" viewBox="0 0 100 100"><path d="M50 4C53 4 55 7 56 11L63 42A20 20 0 1 1 37 42L44 11C45 7 47 4 50 4Z"/><line x1="50" y1="11" x2="50" y2="46"/></svg>';
function knob(p,o){const look=o.look||'flat_ring',v=p.steps>2?'stepped':p.bipolar?'bipolar':'normal';const el=base('pg-knob',p,o,'pg-dial');el.dataset.look=look;el.dataset.var=v;
  /* Pangea patch: felt_ring, Pattern Player's knob (docs/felt-look.md): a raised disc, a hairline ring at its edge with the value as an arc on it, and a tick from inside the disc to the ring. No scale marks; a stepped one has dots just outside. */
  const ring={flat_ring:41,hero:46.5,felt_ring:48}[look],tr={flat_ring:[47.5,50.5],metal_cap:[44,49.5],bakelite_cap:[45.5,50],chicken_head:[43,49],hero:[37.5,41],felt_ring:[50,53]}[look];
  const C='cx="50" cy="50" pathLength="360" transform="rotate(135 50 50)"';let s='<svg class="pg-scale" viewBox="0 0 100 100">';
  if(ring)s+='<circle class="pg-trk" r="'+ring+'" '+C+'/><circle class="pg-glowarc" r="'+ring+'" '+C+'/><circle class="pg-arc" r="'+ring+'" '+C+'/>';
  else if(v==='bipolar')s+='<circle class="pg-arc pg-arc--thin" r="52.5" '+C+'/>';
  const pt=(n,r)=>{const a=(-135+270*n)*Math.PI/180;return [(50+r*Math.sin(a)).toFixed(2),(50-r*Math.cos(a)).toFixed(2)]};
  if(v==='stepped'){const N=Math.min(p.steps,25),r=(tr[0]+tr[1])/2+(look==='flat_ring'?1:0);for(let i=0;i<N;i++){const q=pt(i/(N-1),r);s+='<circle class="pg-dot" cx="'+q[0]+'" cy="'+q[1]+'" r="'+(look==='hero'?1.1:1.7)+'"/>'}}
  else if(look!=='flat_ring'&&look!=='felt_ring'/* Pangea patch: no marks on felt_ring */){const N=look==='hero'?31:11;let d='';for(let i=0;i<N;i++){const a=pt(i/(N-1),tr[0]),b=pt(i/(N-1),tr[1]);d+='M'+a[0]+' '+a[1]+'L'+b[0]+' '+b[1]}s+='<path class="pg-tk" d="'+d+'"/>'}
  if(v==='bipolar')s+='<path class="pg-ctr" d="M46.6 -7h6.8L50 -1.2z"/>';
  el._hit.innerHTML=s+'</svg><div class="pg-cap"></div><div class="pg-rot">'+(look==='chicken_head'?CH:'<i class="pg-ptr"></i>')+'</div>';
  finish(el,o);drag(el,'y',180);return el}
function macro(p,o){const el=knob(p,Object.assign({},o,{look:'hero'}));el.classList.add('pg-macro');if(o.hero)el.classList.add('pg-macro--hero');
  const t=o.targets||[];el.append(h('pg-mlinks',t.length?'<i class="pg-link"></i>'+t.map(esc).join(' · '):''));return el}
function rotary(p,o){const el=knob(p,Object.assign({},o,{look:o.knobLook||'chicken_head'}));el.classList.add('pg-rotary');const N=p.choices.length;
  /* Pangea patch: every position's whole name, never cut to four letters. A name sits just outside the knob at its position's angle and runs away from the knob: right-aligned on the left side, left-aligned on the right side, centred above the top (data-side l, r, t), so names cross neither each other nor the knob. With five positions or fewer, a name of two or more words in the knob's upper half may take two lines (data-wrap, --rw its line in units, rlChars). ctx.ctl widens the control to hold them all (rotaryNeed). One position draws no name. */
  if(N>1&&N<=8){p.choices.forEach((c,i)=>{const a=(-135+270*i/(N-1))*Math.PI/180,sx=Math.sin(a),t=String((p.short&&p.short[i])||c),s=h('pg-rl',esc(t),'span');s.dataset.side=sx<-.3?'l':sx>.3?'r':'t';
      if(rlWrap(N,a)&&/\s/.test(t.trim())){s.dataset.wrap='';s.style.setProperty('--rw',(rlChars(t,true)*(o.cwn||7.8)*1.05+1).toFixed(1))}
      s.style.setProperty('--sx',sx.toFixed(3));s.style.setProperty('--sy',Math.cos(a).toFixed(3));el._hit.append(s)});
    el._ext=n=>{const k=Math.round(n*(N-1));el._hit.querySelectorAll('.pg-rl').forEach((s,i)=>s.classList.toggle('is-on',i===k))};el.apply()}
  return el}

/* ---------- faders: flat_line, channel_strip · vertical, horizontal · normal, bipolar ---------- */
function fader(p,o,orient){const el=base('pg-fader',p,o,'pg-ftrack');el.dataset.look=o.look||'flat_line';el.dataset.orient=orient;el.dataset.var=p.bipolar?'bipolar':'normal';
  el._hit.innerHTML='<i class="pg-fscale"></i><i class="pg-fslot"></i><i class="pg-ffill"></i><i class="pg-fthumb"></i>';
  finish(el,o);drag(el,orient==='h'?'x':'y',()=>Math.max(40,(orient==='h'?el._hit.clientWidth:el._hit.clientHeight)*.82));return el}

/* ---------- switches and buttons: toggle, rocker, lit_button (latching, momentary), segmented ---------- */
function sw(p,o,look){const el=base('pg-switch',p,o);el.dataset.look=look;const mom=p.kind==='momentary';if(mom)el.dataset.mom='1';
  const b=h('pg-sw','<i></i>','button');b.tabIndex=-1;el._hit.append(b);finish(el,o);const hit=el._hit;
  if(mom){hit.addEventListener('pointerdown',()=>el.setN(1));['pointerup','pointerleave','pointercancel'].forEach(ev=>hit.addEventListener(ev,()=>el.setN(0)))}
  else hit.addEventListener('click',()=>el.setN(el._n>=.5?0:1));
  hit.addEventListener('pointerdown',()=>el.classList.add('is-press'));['pointerup','pointerleave','pointercancel'].forEach(ev=>hit.addEventListener(ev,()=>el.classList.remove('is-press')));
  el.addEventListener('keydown',e=>{if(e.key===' '||e.key==='Enter'){e.preventDefault();el.setN(el._n>=.5?0:1)}});return el}
function seg(p,o){const el=base('pg-segc pg-noval',p,o,'pg-seg');el.dataset.look='segmented';const ch=p.kind==='toggle'?['Off','On']:p.choices;
  el._hit.innerHTML=ch.map((c,i)=>'<button class="pg-seg-i" tabindex="-1" data-i="'+i+'">'+esc(c)+'</button>').join('');
  el._ext=n=>{const k=Math.round(n*(ch.length-1));el._hit.querySelectorAll('.pg-seg-i').forEach((b,i)=>b.classList.toggle('is-on',i===k))};
  finish(el,o);el._hit.addEventListener('click',e=>{const b=e.target.closest('.pg-seg-i');if(b)el.setN(+b.dataset.i/(ch.length-1))});
  el.addEventListener('keydown',e=>{const k={ArrowRight:1,ArrowLeft:-1}[e.key];if(k){e.preventDefault();el.setN(el._n+k/(ch.length-1))}});return el}

/* ---------- selectors: dropdown, stepped_rotary, number_box, tempo_sync ---------- */
function dd(p,o){const el=base('pg-ddc pg-noval',p,o,'pg-dd');el.dataset.look='dropdown';
  el._hit.innerHTML='<span class="pg-dd-v"></span><i class="pg-chev"></i><div class="pg-menu">'+p.choices.map((c,i)=>'<b data-i="'+i+'">'+esc(c)+'</b>').join('')+'</div>';
  const val=el._hit.firstChild,menu=el._hit.querySelector('.pg-menu');
  el._ext=n=>{const k=Math.round(n*(p.steps-1));val.textContent=p.choices[k];[...menu.children].forEach((b,i)=>b.classList.toggle('is-sel',i===k))};finish(el,o);
  el._hit.addEventListener('click',e=>{const b=e.target.closest('b');if(b){el.setN(+b.dataset.i/(p.steps-1));el.classList.remove('is-open');return}
    const open=!el.classList.contains('is-open');document.querySelectorAll('.pg-ctl.is-open').forEach(x=>x.classList.remove('is-open'));
    if(open){const r=el.getBoundingClientRect(),pr=(el.closest('.pg-plugin')||document.documentElement).getBoundingClientRect();menu.classList.toggle('is-up',(r.top+r.bottom)/2>pr.top+pr.height*.6);el.classList.add('is-open')}});
  el.addEventListener('keydown',e=>{const k={ArrowDown:1,ArrowUp:-1}[e.key];if(k){e.preventDefault();el.setN(el._n+k/(p.steps-1))}});return el}
if(typeof document!=='undefined')document.addEventListener('pointerdown',e=>{document.querySelectorAll('.pg-ctl.is-open').forEach(x=>{if(!x.contains(e.target))x.classList.remove('is-open')})});
function nb(p,o){const el=base('pg-nbc pg-noval',p,o,'pg-nb');el.dataset.look='number_box';
  el._hit.innerHTML='<span class="pg-nb-v"></span><span class="pg-nb-a"><i data-d="1"></i><i data-d="-1"></i></span>';const val=el._hit.firstChild;
  el._ext=n=>{val.textContent=fmt(p,toVal(p,n))};finish(el,o);drag(el,'y',170);
  el._hit.addEventListener('click',e=>{const d=e.target.dataset&&e.target.dataset.d;if(d)el.setN(el._n+d/(p.steps>1?p.steps-1:50))});return el}
const DIV=['1/1','1/2','1/4','1/8','1/16','1/32'];
function tempo(p,o){const el=base('pg-tsc pg-noval',p,o,'pg-ts');el.dataset.look='tempo_sync';const D=p.choices||DIV,st=el._ts={sync:p.sync!==false,mod:p.mod||0};p.steps=st.sync?D.length:0;
  el._hit.innerHTML='<button class="pg-ts-s" tabindex="-1">Sync</button><span class="pg-ts-d"><i data-d="-1"></i><b></b><i data-d="1"></i></span><span class="pg-ts-m"><button tabindex="-1" data-m="0">–</button><button tabindex="-1" data-m="1">•</button><button tabindex="-1" data-m="2">3</button></span>';
  const q=s=>el._hit.querySelector(s),b=q('b'),sb=q('.pg-ts-s'),mm=q('.pg-ts-m');
  el._ext=n=>{b.textContent=st.sync?D[Math.round(n*(D.length-1))]+['',' D',' T'][st.mod]:fmt({unit:'ms'},20*Math.pow(100,n));sb.classList.toggle('is-on',st.sync);mm.classList.toggle('is-off',!st.sync);[...mm.children].forEach((x,i)=>x.classList.toggle('is-on',i===st.mod))};
  finish(el,o);drag(el,'y',170);
  el._hit.addEventListener('click',e=>{const t=e.target;if(t===sb){st.sync=!st.sync;p.steps=st.sync?D.length:0;el._n=snap(p,el._n);el.apply()}else if(t.dataset.m!=null&&st.sync){st.mod=+t.dataset.m;el.apply()}else if(t.dataset.d)el.setN(el._n+t.dataset.d/(st.sync?D.length-1:40))});return el}

/* ---------- meters: vu_needle, led_ladder, flat_bar, gain_reduction — driven by setLevel(0..1) ---------- */
function meter(o){const type=o.type||'flat_bar',size=o.size||'m';const el=h('pg-ctl pg-meter');el.dataset.look=type;el.dataset.size=size;if(o.state)el.dataset.state=o.state;const hit=el._hit=h('pg-hit');let set;
  if(type==='vu_needle'){hit.className+=' pg-vu';const R=104,pv=138,pt=(q,r)=>{const a=(-44+88*q)*Math.PI/180;return [(100+r*Math.sin(a)).toFixed(1),(pv-r*Math.cos(a)).toFixed(1)]};
    const arc=(a,b)=>{const A=pt(a,R),B=pt(b,R);return 'M'+A[0]+' '+A[1]+'A'+R+' '+R+' 0 0 1 '+B[0]+' '+B[1]};let s='<svg class="pg-on-screen" viewBox="0 0 200 112"><path class="sc" d="'+arc(0,.72)+'"/><path class="hot" d="'+arc(.72,1)+'"/>';
    [['20',0],['10',.2],['7',.32],['5',.42],['3',.54],['0',.72],['+3',1]].forEach((m,i)=>{const a=pt(m[1],R),b=pt(m[1],R-9),c=pt(m[1],R-21),hc=m[1]>=.72?' h':'';s+='<line class="tk'+hc+'" x1="'+a[0]+'" y1="'+a[1]+'" x2="'+b[0]+'" y2="'+b[1]+'"/>';if(i!==2&&i!==4)s+='<text class="'+hc+'" x="'+c[0]+'" y="'+(+c[1]+5)+'">'+m[0]+'</text>'});
    hit.innerHTML=s+'<text x="100" y="102">VU</text><line class="nd" x1="100" y1="'+pv+'" x2="100" y2="40"/></svg>';const nd=hit.querySelector('.nd');set=l=>{nd.style.transform='rotate('+((l-.5)*88).toFixed(2)+'deg)'}}
  /* Pangea patch: o.stereo===true draws two real columns at any size, the second set from setLevel's right level, never a copy of the left. */
  else if(type==='led_ladder'){const n={s:8,m:12,l:16}[size],two=o.stereo===true,ch=two?2:size==='s'||o.stereo===false?1:2;hit.className+=' pg-ledw pg-on-screen';hit.style.setProperty('--n',n);hit.innerHTML='<div class="pg-led"><i></i></div>'.repeat(ch);const is=hit.querySelectorAll('i');set=(l,r)=>is.forEach((i,k)=>i.style.setProperty('--lv',Math.round(clamp(two?(k&&r!=null?r:l):l*(k?.93:1))*n)/n))}
  else if(type==='gain_reduction'){hit.className+=' pg-gr';hit.innerHTML='<span class="pg-gr-sc">'+[0,3,6,12,20].map(d=>'<em style="top:'+(d/20*100)+'%">'+d+'</em>').join('')+'</span><span class="pg-gr-bar pg-on-screen"><i></i></span>';const f=hit.querySelector('.pg-gr-bar i');set=l=>{f.style.transform='scaleY('+l.toFixed(3)+')'}}
  /* Pangea patch: a stereo plain bar, two real bars side by side, each with its own peak line, set with setLevel(left,right). */
  else if(type==='flat_bar'&&o.stereo===true){hit.className+=' pg-bars';hit.innerHTML='<span class="pg-bar"><i></i><b></b></span>'.repeat(2);const cs=[...hit.children],pk=[0,0];set=(l,r)=>cs.forEach((c,k)=>{const v=k&&r!=null?r:l;pk[k]=Math.max(v,pk[k]-.004);c.firstChild.style.transform='scaleY('+v.toFixed(3)+')';c.lastChild.style.transform='translateY('+((1-pk[k])*100).toFixed(1)+'%)'})}
  else{hit.className+=' pg-bar';hit.innerHTML='<i></i><b></b>';const f=hit.firstChild,b=hit.lastChild;let pk=0;set=l=>{pk=Math.max(l,pk-.004);f.style.transform='scaleY('+l.toFixed(3)+')';b.style.transform='translateY('+((1-pk)*100).toFixed(1)+'%)'}}
  /* Pangea patch: setLevel(left,right); the right level is read by a stereo meter only. */
  el.setLevel=(l,r)=>set(clamp(l),r==null?null:clamp(r));el.append(hit,h('pg-lbl','<span>'+esc(o.label||{vu_needle:'Output',led_ladder:'Level',flat_bar:'Level',gain_reduction:'GR'}[type])+'</span>'));el._fp=SZ[type][size];
  el.setLevel(o.level!=null?o.level:.6);
  if(o.live){const sd=Math.random()*9;el.frame=t=>el.setLevel(type==='gain_reduction'?.26+.2*Math.sin(t*3+sd)+.1*Math.sin(t*7.7):sig(t,sd));addLive(el)}
  return el}

/* ---------- displays: waveform, spectrum, envelope, filter_response (+ lfo, curve for the modulation view) ---------- */
const fHz=f=>fmt({unit:'Hz'},20*Math.pow(1000,f));
function display(o){const kind=o.kind||'spectrum',Wd=400,H=160,B=H-12,T=14;const el=h('pg-ctl pg-disp');el.dataset.kind=kind;el.dataset.size=o.size||'m';if(o.state)el.dataset.state=o.state;
  const scr=h('pg-scr pg-on-screen');let g='';
  if(kind==='spectrum'||kind==='filter_response'){[50,100,200,500,1000,2000,5000,10000].forEach(f=>{g+='M'+(Math.log10(f/20)/3*Wd).toFixed(1)+' 0V'+H});[.25,.5,.75].forEach(y=>{g+='M0 '+H*y+'H'+Wd})}
  else [.25,.5,.75].forEach(y=>{g+='M0 '+H*y+'H'+Wd+'M'+Wd*y+' 0V'+H});
  scr.innerHTML='<svg viewBox="0 0 400 160" preserveAspectRatio="none"><path class="pg-d-grid" d="'+g+'"/><path class="pg-d-back"/><path class="pg-d-fill"/><path class="pg-d-line"/></svg><span class="pg-d-tag">'+esc(o.label||kind.replace('_',' '))+'</span>';
  const ps=scr.querySelectorAll('path'),back=ps[1],fill=ps[2],line=ps[3],chip=h('pg-chip');scr.append(chip);el.append(scr);
  const sd=Math.random()*9,spec=(x,t)=>clamp(.8-.56*x+.15*Math.sin(x*9+t*1.3+sd)+.1*Math.sin(x*23-t*2.1)+.06*Math.sin(x*51+t*3.7),.02,1)*Math.sin(Math.min(1,.12+x*7)*Math.PI/2);
  const st=el._d={env:Object.assign({a:.18,d:.4,s:.55,r:.45},o.env),bands:o.bands||[{f:.2,g:.35,q:.5},{f:.43,g:-.4,q:.65},{f:.64,g:.3,q:.4},{f:.87,g:-.2,q:.5}],shape:o.shape||0,curve:o.curve!=null?o.curve:.5,t:o.t||0};
  const lp=ys=>ys.map((y,i)=>(i?'L':'M')+(i/(ys.length-1)*Wd).toFixed(1)+' '+y.toFixed(1)).join('');
  const resp=x=>st.bands.reduce((a,b)=>{const t=b.type||'bell';return a+(t==='hp'?-Math.min(1.2,Math.max(0,b.f-x)*9):t==='lp'?-Math.min(1.2,Math.max(0,x-b.f)*9):t==='ls'?b.g/(1+Math.exp((x-b.f)/.035)):t==='hs'?b.g/(1+Math.exp((b.f-x)/.035)):b.g/(1+Math.pow((x-b.f)/(.03+.11*(1-(b.q==null?.5:b.q))),2)))},0);
  const hands=[];let hot=-1;
  const hp=()=>{const e=st.env,x1=8+e.a*110,x2=x1+10+e.d*110,ys=B-e.s*(B-T-14),x4=298+e.r*94;return kind==='envelope'?[[x1,T],[x2,ys],[x4,B]]:st.bands.map(b=>[b.f*Wd,H/2-b.g*H*.42])};
  const chipTxt=i=>{if(kind==='envelope'){const e=st.env;return i===0?'Attack '+Math.round(2*Math.pow(1000,e.a))+' ms':i===1?'Decay '+Math.round(5*Math.pow(800,e.d))+' ms · Sustain '+Math.round(e.s*100)+' %':'Release '+Math.round(5*Math.pow(1600,e.r))+' ms'}const b=st.bands[i];return fHz(b.f)+(/^(hp|lp)$/.test(b.type||'')?'':' · '+(b.g>0?'+':'')+(b.g*18).toFixed(1)+' dB')};
  el.draw=()=>{const t=st.t;
    if(kind==='spectrum'){const ys=[];for(let i=0;i<64;i++)ys.push(H-spec(i/63,t)*H*.9);const d=lp(ys);line.setAttribute('d',d);fill.setAttribute('d',d+'L400 160L0 160Z')}
    else if(kind==='waveform'){const a=[],b=[];for(let i=0;i<96;i++){const v=Math.abs(Math.sin(i*.31+t*2.2+sd))*(.25+.7*Math.abs(Math.sin(i*.067+t*.5)))*H*.44+1;a.push(H/2-v);b.push(H/2+v)}const d=lp(a);let bk='';for(let i=95;i>=0;i--)bk+='L'+(i/95*Wd).toFixed(1)+' '+b[i].toFixed(1);fill.setAttribute('d',d+bk+'Z');line.setAttribute('d',d+lp(b))}
    else if(kind==='envelope'){const q=hp(),x1=q[0][0],x2=q[1][0],ys=q[1][1],x4=q[2][0];const d='M8 '+B+'Q'+(8+(x1-8)*.35).toFixed(1)+' '+T+' '+x1.toFixed(1)+' '+T+'Q'+(x1+(x2-x1)*.25).toFixed(1)+' '+ys.toFixed(1)+' '+x2.toFixed(1)+' '+ys.toFixed(1)+'L298 '+ys.toFixed(1)+'Q'+(298+(x4-298)*.25).toFixed(1)+' '+B+' '+x4.toFixed(1)+' '+B;line.setAttribute('d',d);fill.setAttribute('d',d+'L8 '+B+'Z')}
    else if(kind==='filter_response'){const ys=[];for(let i=0;i<=100;i++)ys.push(clamp(H/2-resp(i/100)*H*.42,2,H-2));const d=lp(ys);line.setAttribute('d',d);fill.setAttribute('d',d+'L400 80L0 80Z');
      if(o.spectrum){const bs=[];for(let i=0;i<64;i++)bs.push(H-spec(i/63,t)*H*.8);back.setAttribute('d',lp(bs)+'L400 160L0 160Z')}}
    else if(kind==='lfo'){const ys=[];for(let i=0;i<=120;i++){const ph=(i/60+t*.4)%1,k=st.shape;ys.push(H/2-(k===0?Math.sin(ph*6.283):k===1?1-4*Math.abs(ph-.5):k===2?1-2*ph:ph<.5?1:-1)*H*.36)}line.setAttribute('d',lp(ys));fill.setAttribute('d','')}
    else{const ys=[];for(let i=0;i<=40;i++)ys.push(B-Math.pow(i/40,Math.pow(4,1-st.curve*2))*(B-T));const d=lp(ys);line.setAttribute('d',d);fill.setAttribute('d',d+'L400 '+B+'L0 '+B+'Z')}
    if(hands.length){hp().forEach((q,i)=>{hands[i].style.left=(q[0]/Wd*100).toFixed(2)+'%';hands[i].style.top=(q[1]/H*100).toFixed(2)+'%'});if(hot>=0){chip.style.left=hands[hot].style.left;chip.style.top=hands[hot].style.top;chip.textContent=chipTxt(hot)}}};
  if(o.handles&&(kind==='envelope'||kind==='filter_response')){hp().forEach((q,i)=>{const hd=h('pg-hand',kind==='envelope'?'ADR'[i]:String(i+1),'i');scr.append(hd);hands.push(hd);let last=null;
    const show=()=>{hot=i;el.classList.add('is-chip');el.draw()};
    hd.addEventListener('pointerenter',show);hd.addEventListener('pointerleave',()=>{if(!last){el.classList.remove('is-chip');hot=-1}});
    hd.addEventListener('pointerdown',e=>{last=[e.clientX,e.clientY];try{hd.setPointerCapture(e.pointerId)}catch(_){}hd.classList.add('is-drag');show();e.preventDefault()});
    hd.addEventListener('pointermove',e=>{if(!last)return;const r=scr.getBoundingClientRect(),k=e.shiftKey?.125:1,dx=(e.clientX-last[0])/r.width*k,dy=(e.clientY-last[1])/r.height*k;last=[e.clientX,e.clientY];
      if(kind==='envelope'){const en=st.env;if(i===0)en.a=clamp(en.a+dx*Wd/110);else if(i===1){en.d=clamp(en.d+dx*Wd/110);en.s=clamp(en.s-dy*H/(B-T-14))}else en.r=clamp(en.r+dx*Wd/94)}
      else{const b=st.bands[i];b.f=clamp(b.f+dx,.01,.99);b.g=clamp(b.g-dy/.42,-1,1)}
      el.draw();if(el.onHandle)el.onHandle(i,st)});
    const up=()=>{last=null;hd.classList.remove('is-drag')};hd.addEventListener('pointerup',up);hd.addEventListener('pointercancel',up)});
    if(o.state==='hover'||o.state==='active'){hot=1;hands[1].classList.add(o.state==='hover'?'is-hover':'is-drag');el.classList.add('is-chip')}}
  el._fp=SZ.display[o.size||'m'];el.draw();
  if(o.live&&(kind==='spectrum'||kind==='waveform'||kind==='lfo'||(kind==='filter_response'&&o.spectrum))){el.frame=t=>{st.t=t;el.draw()};addLive(el)}
  return el}

/* ---------- XY pad ---------- */
function xy(o){const px=o.x,py=o.y,el=h('pg-ctl pg-xy');el.dataset.size=o.size||'m';if(o.state)el.dataset.state=o.state;el.tabIndex=0;
  const pad=h('pg-hit pg-xy-pad pg-on-screen','<i class="pg-xy-h"></i><i class="pg-xy-v"></i><i class="pg-xy-p"></i>'),chip=h('pg-chip');pad.append(chip);
  el.append(h('pg-xy-yl','<span>'+esc(py.label)+'</span>'),pad,h('pg-lbl','<span>'+esc(px.label)+'</span>'));
  el._x=o.nx!=null?o.nx:toN(px,px.default);el._y=o.ny!=null?o.ny:toN(py,py.default);
  el.apply=()=>{el.style.setProperty('--x',el._x);el.style.setProperty('--y',el._y);chip.textContent=fmt(px,toVal(px,el._x))+' · '+fmt(py,toVal(py,el._y))};
  /* Pangea patch: onInput is told which axes moved, so a move on one axis writes only that axis's parameter, and a host's Touch or Latch automation of the other is left alone (docs/felt-look.md). */
  el.setXY=(x,y,silent)=>{const nx=snap(px,clamp(x)),ny=snap(py,clamp(y)),mx=nx!==el._x,my=ny!==el._y;el._x=nx;el._y=ny;el.apply();if(!silent&&el.onInput&&(mx||my))el.onInput(el._x,el._y,mx,my)};
  let last=null;pad.addEventListener('pointerdown',e=>{const r=pad.getBoundingClientRect();last=[e.clientX,e.clientY];try{pad.setPointerCapture(e.pointerId)}catch(_){}el.classList.add('is-drag');if(!e.shiftKey)el.setXY((e.clientX-r.left)/r.width,1-(e.clientY-r.top)/r.height)});
  pad.addEventListener('pointermove',e=>{if(!last)return;const r=pad.getBoundingClientRect(),k=e.shiftKey?.125:1;el.classList.toggle('is-fine',e.shiftKey);el.setXY(el._x+(e.clientX-last[0])/r.width*k,el._y-(e.clientY-last[1])/r.height*k);last=[e.clientX,e.clientY]});
  const up=()=>{last=null;el.classList.remove('is-drag','is-fine')};pad.addEventListener('pointerup',up);pad.addEventListener('pointercancel',up);
  /* Pangea patch: the pad from the keyboard, as a knob is: the arrows move its point across and up, a fiftieth of the way, with Shift a four hundredth; a double press puts both controls back where they start (docs/felt-look.md). */
  el.addEventListener('keydown',e=>{const d={ArrowLeft:-1,ArrowRight:1,ArrowUp:2,ArrowDown:-2}[e.key];if(!d)return;e.preventDefault();const k=e.shiftKey?.0025:.02,dx=Math.abs(d)===1?d:0,dy=Math.abs(d)===2?d/2:0;el.setXY(el._x+dx*(px.steps>1?1/(px.steps-1):k),el._y+dy*(py.steps>1?1/(py.steps-1):k))});
  pad.addEventListener('dblclick',()=>el.setXY(toN(px,px.default),toN(py,py.default)));
  el._fp=SZ.xy_pad[o.size||'m'];el.apply();return el}

/* ---------- Pangea patch: the variation, the chord map and the notes (docs/felt-look.md) ---------- */
/* generate: a variation number as Pattern Player's bottom bar has it. Generate moves to the next number (from the last back to the first above 0), the arrows step back and on, and beside them the control's own label stands over its number, 0 as Original. A stepped parameter like a number box: the same value, the same events. */
function generate(p,o){const el=base('pg-gen',p,o,'pg-gen-in'),lo=Math.round(p.min),hi=Math.round(p.max);el.tabIndex=-1;
  el._hit.innerHTML='<button type="button" class="pg-gen-go">Generate</button><span class="pg-gen-st"><button type="button" data-g="-1" aria-label="Previous variation"><i></i></button><button type="button" data-g="1" aria-label="Next variation"><i></i></button></span>';
  const shown=h('pg-gen-v',null,'span'),now=()=>Math.round(toVal(p,el._n)),go=v=>el.setN(toN(p,clamp(v,lo,hi)));
  el._hit.addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;if(b.dataset.g)go(now()+ +b.dataset.g);else go(now()>=hi?Math.max(1,lo):now()+1)});
  finish(el,o);const col=h('pg-gen-c');col.append(el._lbl,shown);el.append(col);el._ext=()=>{const v=now();shown.textContent=v===0?'Original':String(v)};el.apply();el.classList.add('pg-noval');return el}
/* The chord map: the chords of a key on Pattern Player's ring, in thirds, home at the top. Neighbours share two notes (a heavy line), chords two apart share one (a light line). Each chord carries its job's mark (home a dot, departure a ring, tension a diamond), so it is never told by colour alone. The chord heard now is filled, the one before it ringed, and a tension chord points home. The host tells it what to show (show), and a press on a chord asks the host (onChoose), which changes the chord only where the tool has a control for it. */
function chordMap(o){const el=h('pg-ctl pg-cmap');el.setAttribute('role','group');el.dataset.size=o.size||'m';el.innerHTML='<div class="pg-cmap-t"><span>'+esc(o.label||'Chord map')+'</span><em></em></div><svg class="pg-cmap-s" viewBox="0 0 300 300"></svg>';
  const svg=el.querySelector('svg'),tag=el.querySelector('em'),C=150,R=106,NR=29,at=(i,n)=>{const a=2*Math.PI*i/n;return [C+R*Math.sin(a),C-R*Math.cos(a)]};
  const mark=(role,x,y,s)=>role==='home'?'<circle class="pg-cm-'+role+'" cx="'+x.toFixed(1)+'" cy="'+y.toFixed(1)+'" r="'+s/2+'"/>':role==='departure'?'<circle class="pg-cm-'+role+'" cx="'+x.toFixed(1)+'" cy="'+y.toFixed(1)+'" r="'+(s/2-.6)+'" fill="none" stroke-width="1.2"/>':'<path class="pg-cm-'+role+'" d="M'+x.toFixed(1)+' '+(y-s/1.6).toFixed(1)+'L'+(x+s/1.6).toFixed(1)+' '+y.toFixed(1)+'L'+x.toFixed(1)+' '+(y+s/1.6).toFixed(1)+'L'+(x-s/1.6).toFixed(1)+' '+y.toFixed(1)+'Z"/>';
  /* Drawn again only when what it shows changes, so a press is never lost to a drawing in the middle of it (the review of 19 November: half the presses were, when it was drawn four times a second). */
  let state=null,shown=null;el.show=st=>{const next=st||{ring:[]},sig=JSON.stringify([next.ring,next.degree,next.previous,next.chord,next.role,next.keyName,next.keyFrom,next.playing,!!el.onChoose]);state=next;if(sig===shown)return;shown=sig;const ring=state.ring||[],n=ring.length,now=state.degree,before=state.previous;let g='';tag.textContent=state.keyName?(state.keyName+(state.keyFrom==='notes'?', heard':'')):'listening';
    for(let i=0;i<n;i++){const a=at(i,n),b=at(i+1,n),c=at(i+2,n);g+='<line class="pg-cm-near" x1="'+a[0].toFixed(1)+'" y1="'+a[1].toFixed(1)+'" x2="'+b[0].toFixed(1)+'" y2="'+b[1].toFixed(1)+'"/><line class="pg-cm-far" x1="'+a[0].toFixed(1)+'" y1="'+a[1].toFixed(1)+'" x2="'+c[0].toFixed(1)+'" y2="'+c[1].toFixed(1)+'"/>'}
    const posOf=d=>ring.findIndex(r=>r.number===d);
    if(now!=null&&state.role==='tension'&&posOf(now)>0){const f=at(posOf(now),n),t=at(0,n),dx=t[0]-f[0],dy=t[1]-f[1],L=Math.hypot(dx,dy)||1,ux=dx/L,uy=dy/L,x1=f[0]+ux*(NR+4),y1=f[1]+uy*(NR+4),x2=t[0]-ux*(NR+7),y2=t[1]-uy*(NR+7);
      g+='<line class="pg-cm-arrow" x1="'+x1.toFixed(1)+'" y1="'+y1.toFixed(1)+'" x2="'+x2.toFixed(1)+'" y2="'+y2.toFixed(1)+'"/><path class="pg-cm-head" d="M'+(x2+ux*6).toFixed(1)+' '+(y2+uy*6).toFixed(1)+'L'+(x2-uy*4.5).toFixed(1)+' '+(y2+ux*4.5).toFixed(1)+'L'+(x2+uy*4.5).toFixed(1)+' '+(y2-ux*4.5).toFixed(1)+'Z"/>'}
    ring.forEach((r,i)=>{const p=at(i,n),lit=r.number===now,was=!lit&&r.number===before;
      /* Pangea patch: each chord reached with Tab and named for a screen reader; a button where a press sets the tool's chord. */
      const name=r.name+', chord '+r.number+', '+r.role+(lit?', playing now':was?', the chord before':'');
      g+='<g class="pg-cm-node'+(lit?' is-now':'')+(was?' is-before':'')+'" data-degree="'+r.number+'" data-lit="'+(lit?1:0)+'" tabindex="0" role="'+(el.onChoose?'button':'img')+'" aria-label="'+esc(name)+'"><circle class="pg-cm-disc" cx="'+p[0].toFixed(1)+'" cy="'+p[1].toFixed(1)+'" r="'+NR+'"/>'+mark(r.role,p[0]-7,p[1]-NR*.48,6)+
        '<text class="pg-cm-num" x="'+(p[0]+6).toFixed(1)+'" y="'+(p[1]-NR*.48+3.5).toFixed(1)+'">'+r.number+'</text><text class="pg-cm-name" x="'+p[0].toFixed(1)+'" y="'+(p[1]+NR*.42).toFixed(1)+'" font-size="'+(String(r.name).length>6?11:13)+'">'+esc(r.name)+'</text></g>'});
    const ch=state.chord;g+='<rect class="pg-cm-mid" x="'+(C-56)+'" y="'+(C-34)+'" width="112" height="68" rx="7"/><text class="pg-cm-chord" x="'+C+'" y="'+(C-7)+'">'+(ch?esc(ch.name):'...')+'</text><text class="pg-cm-notes" x="'+C+'" y="'+(C+10)+'">'+(ch?esc(ch.notes.join(' ')):(state.playing===false?'PRESS PLAY':'LISTENING'))+'</text>';
    if(ch&&state.role){const w=state.role.length*7.4+12;g+=mark(state.role,C-w/2+3,C+23,6)+'<text class="pg-cm-job pg-cm-'+state.role+'" x="'+(C-w/2+12).toFixed(1)+'" y="'+(C+26)+'">'+state.role.toUpperCase()+'</text>'}
    else if(ch)g+='<text class="pg-cm-job pg-cm-out" x="'+C+'" y="'+(C+26)+'" text-anchor="middle">OUTSIDE THE KEY</text>';
    const had=document.activeElement&&svg.contains(document.activeElement)&&document.activeElement.closest('.pg-cm-node');
    svg.innerHTML=g;el.dataset.chord=ch?ch.name:'';el.classList.toggle('can-choose',!!el.onChoose);el.setAttribute('aria-label',(o.label||'Chord map')+(state.keyName?', '+state.keyName:''));
    /* the chord that had the keyboard keeps it when the map is drawn again */
    if(had){const back=svg.querySelector('.pg-cm-node[data-degree="'+had.dataset.degree+'"]');if(back)back.focus()}};
  svg.addEventListener('keydown',e=>{if(!(e.key==='Enter'||e.key===' '))return;const node=e.target.closest&&e.target.closest('.pg-cm-node');if(node&&el.onChoose){e.preventDefault();el.onChoose(+node.dataset.degree,state)}});
  /* A chord is chosen as the pointer goes down on it, as a pad is struck. */
  svg.addEventListener('pointerdown',e=>{if(e.button)return;const node=e.target.closest&&e.target.closest('.pg-cm-node');if(node&&el.onChoose){e.preventDefault();el.onChoose(+node.dataset.degree,state)}});
  el.show(null);el._fp=SZ.chord_map[o.size||'m'];return el}
/* The notes: what the tool played in the last seconds, as a roll, each channel in its own colour (the first the highlight, the second the accent), the Cs named at the left. The host draws it (draw), each frame it has something to show. */
function noteRoll(o){const el=h('pg-ctl pg-roll');el.dataset.size=o.size||'m';el.innerHTML='<div class="pg-cmap-t"><span>'+esc(o.label||'Notes')+'</span><em>'+esc(o.note||'')+'</em></div><canvas class="pg-roll-c"></canvas>';
  const cv=el.querySelector('canvas'),range={low:48,high:72};
  el.draw=(notes,now,seconds,name,empty)=>{const g=cv.getContext&&cv.getContext('2d');if(!g)return;const k=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;if(!W||!H)return;
    if(cv.width!==Math.round(W*k)||cv.height!==Math.round(H*k)){cv.width=Math.round(W*k);cv.height=Math.round(H*k)}g.setTransform(k,0,0,k,0,0);g.clearRect(0,0,W,H);
    const css=getComputedStyle(el),v=n=>css.getPropertyValue(n).trim(),inks=[v('--hl'),v('--acc'),v('--ink-2'),v('--ink')],font=v('--f-ui')||'sans-serif',gut=Math.max(26,W*.06),plot=W-gut;
    if(notes.length){const lo=Math.min.apply(null,notes.map(x=>x.note))-2,hi=Math.max.apply(null,notes.map(x=>x.note))+2,mid=(lo+hi)/2,span=Math.max(24,hi-lo);range.low+=(mid-span/2-range.low)*.08;range.high+=(mid+span/2-range.high)*.08}
    const row=H/(range.high-range.low),y=n=>H-(n-range.low)*row,x=t=>gut+(t-(now-seconds))/seconds*plot;
    g.font='500 '+Math.max(9,H*.075).toFixed(1)+'px '+font;g.lineWidth=1;
    for(let n=Math.ceil(range.low);n<=range.high;n++){if(n%12)continue;const yy=Math.round(y(n))+.5;g.strokeStyle=v('--line');g.beginPath();g.moveTo(gut,yy);g.lineTo(W,yy);g.stroke();g.fillStyle=v('--ink-3');g.fillText(name(n),3,yy+3)}
    notes.forEach(x0=>{const a=Math.max(gut,x(x0.start)),b=x(x0.end==null?now:x0.end),hh=Math.max(4,row*.8),w=Math.max(2,b-a);g.globalAlpha=.6+.4*(x0.velocity/127);g.fillStyle=inks[(x0.channel-1+inks.length)%inks.length]||inks[0];
      g.beginPath();if(g.roundRect)g.roundRect(a,y(x0.note)-hh/2,w,hh,2);else g.rect(a,y(x0.note)-hh/2,w,hh);g.fill();if(x0.end==null){g.globalAlpha=1;g.fillStyle=v('--ink');g.fillRect(a+w-2,y(x0.note)-hh/2,2,hh)}});
    g.globalAlpha=1;if(!notes.length&&empty){g.fillStyle=v('--ink-3');g.textAlign='center';g.fillText(empty,gut+plot/2,H/2+4);g.textAlign='start'}
    el.dataset.notes=String(notes.length)};
  el._fp=SZ.note_roll[o.size||'m'];return el}

/* ---------- step grid: 8 / 16 / 32 steps, on-off, value, accent + slide flags, moving play position ---------- */
function steps(o){const n=o.n||16,size=o.size||'m',flags=o.flags||[],el=h('pg-ctl pg-steps');el.dataset.size=size;if(o.state)el.dataset.state=o.state;
  const hit=h('pg-hit pg-st-wrap'),per=n>16?16:n,rows=Math.ceil(n/per);
  const data=el._steps=o.data||Array.from({length:n},(_,i)=>({on:[1,0,0,1,0,1,0,0,1,0,1,0,0,1,1,0][i%16],v:.3+.65*Math.abs(Math.sin(i*1.7+.4)),accent:i%8===3,slide:i%8===5}));
  let s='';for(let r=0;r<rows;r++){s+='<div class="pg-st-row" style="--n:'+per+'"><i class="pg-st-ph"></i>';for(let i=r*per;i<Math.min(n,(r+1)*per);i++){const d=data[i];
    s+='<div class="pg-st'+(d.on?' is-on':'')+'" data-i="'+i+'"><em class="pg-st-n">'+(i+1)+'</em><span class="pg-st-v" style="--sv:'+d.v.toFixed(2)+'"><i></i></span><span class="pg-st-p"></span>'+(flags.length?'<span class="pg-st-f">'+flags.map(f=>'<i data-f="'+f+'"'+(d[f]?' class="is-on"':'')+'></i>').join('')+'</span>':'')+'</div>'}s+='</div>'}
  hit.innerHTML=s;el.append(hit,h('pg-lbl','<span>'+esc(o.label||'Steps')+'</span>'+(flags.length?'<em class="pg-st-lg">'+flags.map(f=>'<i class="'+(f==='slide'?'s':'a')+'"></i>'+f).join('')+'</em>':'')));
  const emit=()=>{if(el.onInput)el.onInput(data)};let dragV=null;
  hit.addEventListener('click',e=>{const c=e.target.closest('.pg-st');if(!c)return;const d=data[+c.dataset.i];if(e.target.classList.contains('pg-st-p')){d.on=d.on?0:1;c.classList.toggle('is-on',!!d.on);emit()}else if(e.target.dataset.f){const f=e.target.dataset.f;d[f]=!d[f];e.target.classList.toggle('is-on',d[f]);emit()}});
  const setV=e=>{const r=dragV.getBoundingClientRect(),v=clamp(1-(e.clientY-r.top)/r.height);dragV.style.setProperty('--sv',v.toFixed(2));data[+dragV.parentNode.dataset.i].v=v;emit()};
  hit.addEventListener('pointerdown',e=>{const v=e.target.closest('.pg-st-v');if(v){dragV=v;try{v.setPointerCapture(e.pointerId)}catch(_){}setV(e)}});
  hit.addEventListener('pointermove',e=>{if(dragV)setV(e)});['pointerup','pointercancel'].forEach(ev=>hit.addEventListener(ev,()=>{dragV=null}));
  const cells=hit.querySelectorAll('.pg-st'),phs=hit.querySelectorAll('.pg-st-ph');let cur=-1;
  el.setPlay=pos=>{const k=Math.floor(pos)%n,row=Math.floor(k/per);if(k!==cur){if(cur>=0)cells[cur].classList.remove('is-play');cells[k].classList.add('is-play');cur=k}phs.forEach((ph,i)=>{ph.style.opacity=i===row?1:0;if(i===row)ph.style.transform='translateX('+(((pos%n)-row*per)/per*100).toFixed(2)+'%)'})};
  const bh={s:32,m:52,l:84}[size],ph={s:14,m:16,l:20}[size],rh=12+3+bh+3+ph+(flags.length?9:0);el._fp=[(o.cols||56)*8,Math.ceil((rows*rh+(rows-1)*8+36)/8)*8];
  el.setPlay(o.pos!=null?o.pos:2.4);if(o.live){el.frame=t=>el.setPlay(t*(o.bpm||112)/15%n);addLive(el)}
  return el}

/* ---------- control dispatcher ---------- */
function control(type,p,o){o=o||{};let el,key=type;
  switch(type){
    case 'knob':el=knob(p,o);key=o.look==='hero'?'hero':'knob';break;
    case 'hero':el=knob(p,Object.assign({},o,{look:'hero'}));break;
    case 'fader':case 'fader_v':el=fader(p,o,'v');key='fader_v';break;
    case 'fader_h':el=fader(p,o,'h');break;
    case 'toggle':case 'rocker':case 'lit_button':el=sw(p,o,type);break;
    case 'segmented':el=seg(p,o);break;
    case 'dropdown':el=dd(p,o);break;
    case 'stepped_rotary':el=rotary(p,o);break;
    case 'number_box':el=nb(p,o);break;
    case 'tempo_sync':el=tempo(p,o);break;
    /* Pangea patch: a step bar and the variation's Generate (docs/felt-look.md). */
    case 'bar':el=fader(p,Object.assign({},o,{look:'bar'}),'v');break;
    case 'generate':el=generate(p,o);break;
    default:el=knob(p,o);key='knob'}
  el._fp=SZ[key][o.size||'m'];return el}
P.control=control;P.meter=meter;P.display=display;P.xy=xy;P.steps=steps;P.macro=macro;P.chordMap=chordMap;P.noteRoll=noteRoll;/* Pangea patch: chordMap, noteRoll */

/* ---------- layout engine: 8-unit grid, shelf packing, pages. Nothing overlaps by construction. ---------- */
/* Pangea patch: a section may also be laid out in more rows than it needs, narrower and taller (its shapes, from groupBlocks), so that flow can stand it beside a taller block instead of leaving the space empty. shapes[0] is the library's own layout; use(k) picks one before place. A block of one piece (wb) has one shape. */
const rowsOf=(items,target)=>{const rows=[];let r=null;items.forEach(it=>{if(!r||r.w+it.w>target){r={w:0,h:0,items:[]};rows.push(r)}r.items.push(it);r.w+=it.w;r.h=Math.max(r.h,it.h)});return rows};
const shapeOf=rows=>({rows,w:Math.max.apply(null,rows.map(r=>r.w))+2,h:rows.reduce((a,r)=>a+r.h,0)+(rows.length-1)+4});
function mkGroup(title,rows,frame,more){const el=h('pg-group pg-legible');el.dataset.frame=frame;const cells=h('pg-cells');el.append(h('pg-g-t','<span>'+esc(title)+'</span>'),cells);
  rows.forEach(r=>r.items.forEach(it=>cells.append(it.el)));
  const g={el,title,shapes:[shapeOf(rows)].concat(more||[]),use(k){const s=g.shapes[k]||g.shapes[0];rows=s.rows;g.w=s.w;g.h=s.h},place(w){const cols=w-2;cells.style.gridTemplateColumns='repeat('+cols+',calc(8*var(--u)))';let y=1;
    rows.forEach(r=>{const extra=cols-r.w,n=r.items.length;let x=1;r.items.forEach((it,i)=>{x+=Math.round(extra*(i+1)/(n+1))-Math.round(extra*i/(n+1));it.el.style.gridColumn=x+' / span '+it.w;it.el.style.gridRow=(y+r.h-it.h)+' / span '+it.h;x+=it.w});y+=r.h+1})}};g.use(0);return g}
function groupBlocks(title,items,maxCols,maxRows,frame){const inner=maxCols-2,tot=items.reduce((a,i)=>a+i.w,0),nr=Math.ceil(tot/inner);
  const mx=Math.max.apply(null,items.map(i=>i.w)),target=Math.min(inner,Math.max(Math.ceil(tot/nr)+2,mx)),rows=rowsOf(items,target);
  const out=[];let chunk=[],ch=0;const cap=maxRows-4;rows.forEach(rw=>{if(chunk.length&&ch+rw.h>cap){out.push(chunk);chunk=[];ch=0}chunk.push(rw);ch+=rw.h+1});out.push(chunk);
  /* Pangea patch: the narrowest layout of each row count, up to four rows more than the library's own, no taller than a page (a section split over pages has none). */
  const more=[];if(out.length===1&&items.length>1){const seen={};for(let t=mx;t<target;t++){const rs=rowsOf(items,t),n=rs.length;if(n>rows.length&&n<=rows.length+4&&!seen[n]){seen[n]=1;const sh=shapeOf(rs);if(sh.h<=maxRows)more.push(sh)}}}
  return out.map((rs,i)=>mkGroup(out.length>1?title+' '+(i+1):title,rs,frame,out.length>1?null:more))}
function wb(el,title,frame){fix(el);const g=h('pg-group pg-wb pg-legible');g.dataset.frame=frame||'none';if(title)g.append(h('pg-g-t','<span>'+esc(title)+'</span>'));g.append(el);
  const w=Math.ceil(el._fp[0]/8)+2,ht=Math.ceil(el._fp[1]/8)+(title?4:2);return {el:g,title:title,w,h:ht,shapes:[{w,h:ht}],use(){},place(){}}}
/* Pangea patch: flow packs the blocks twice: the library's own way and a fuller way, and draws the fuller one where it costs no page. The library's own: each block in its first shape, in order, a new shelf when the next one does not fit. The fuller one, in the same order: (1) when the next block does not fit, the shelf's blocks and it each take their narrowest shape no taller than the shelf; (2) a shelf less than half full takes the next block at a taller height, when that height is no more than the two shelves it would otherwise make; (3) a shelf less than half full joins the shelf before it on the same terms; (4) each block takes its narrowest shape no taller than its shelf. So a small section no longer stands alone with the page empty on both sides, nor stretched beside a much taller block. The count of pages is still the drawing's own. */
function flow(blocks,cols,maxRows){const W=(b,k)=>b.full?cols:Math.min(b.shapes[k].w,cols),Ht=(b,k)=>b.shapes[k].h;
  const wid=items=>items.reduce((a,it)=>a+W(it.b,it.k),0)+items.length-1,hgt=items=>Math.max.apply(null,items.map(it=>Ht(it.b,it.k)));
  const nar=(b,H)=>{let k=0;b.shapes.forEach((s,i)=>{if(s.h<=H&&W(b,i)<W(b,k))k=i});return k},tight=(items,H)=>items.map(it=>({b:it.b,k:nar(it.b,H)}));
  const sparse=items=>wid(items)*2<cols;
  const join=(items,lim)=>{const H0=Math.max.apply(null,items.map(it=>Ht(it.b,0))),hs=[];items.forEach(it=>it.b.shapes.forEach(sh=>{if(sh.h>=H0&&sh.h<=lim&&hs.indexOf(sh.h)<0)hs.push(sh.h)}));hs.sort((x,y)=>x-y);
    for(const H of hs){const t=tight(items,H);if(wid(t)<=cols)return t}return null};
  const plain=[];let cur=null;blocks.forEach(b=>{const it={b,k:0};if(cur&&wid(cur.concat(it))>cols){plain.push(cur);cur=null}cur=(cur||[]).concat(it)});if(cur)plain.push(cur);
  const fuller=list=>{let full=[],cur=null;list.forEach(b=>{const it={b,k:0};if(!cur){cur=[it];return}let t=cur.concat(it);if(wid(t)<=cols){cur=t;return}
      t=join(cur.concat(it),Math.max(hgt(cur),Ht(b,0)));if(t){cur=t;return}
      if(sparse(cur)){t=join(cur.concat(it),hgt(cur)+1+Ht(b,0));if(t){cur=t;return}}
      full.push(cur);cur=[it]});if(cur)full.push(cur);
    for(let i=1;i<full.length;i++){if(!sparse(full[i])&&!sparse(full[i-1]))continue;const t=join(full[i-1].concat(full[i]),hgt(full[i-1])+1+hgt(full[i]));if(t){full.splice(i-1,2,t);i=Math.max(0,i-2)}}
    return full.map(sh=>tight(sh,hgt(sh)))};
  const pag=shelves=>{const out=[];let page=null,used=0;shelves.forEach(s=>{const H=hgt(s);if(page&&used+H+1>maxRows){out.push(page);page=null}if(!page){page=[];used=H}else used+=H+1;page.push(s)});if(page)out.push(page);return out};
  /* The fuller packing of every block when it needs fewer pages; otherwise the library's own pages, each packed fuller on its own, so no block moves to another page. */
  const A=pag(plain),B=pag(fuller(blocks)),pages=B.length<A.length?B:A.map(page=>{const one=pag(fuller(page.flatMap(sh=>sh.map(it=>it.b))));return one.length===1?one[0]:page});
  const el=h('pg-flow'),pg=h('pg-pages');el.append(pg);
  const els=pages.map((sh,pi)=>{const page=h('pg-page'+(pi?'':' is-on'));page.style.gridTemplateColumns='repeat('+cols+',calc(8*var(--u)))';let y=1;
    sh.forEach((items,si)=>{items.forEach(it=>it.b.use(it.k));const ws=items.map(it=>W(it.b,it.k)),H=hgt(items),sum=ws.reduce((a,w)=>a+w,0),extra=cols-(sum+items.length-1),use=Math.min(extra,sum);let x=1+Math.floor((extra-use)/2),given=0;
      items.forEach((it,i)=>{const b=it.b,addc=i===items.length-1?use-given:Math.floor(use*ws[i]/sum);given+=addc;const w=ws[i]+addc;
        b.el.style.gridColumn=x+' / span '+w;b.el.style.gridRow=y+' / span '+H;b.place(w,H);
        if(i){const sm=h('pg-seam pg-seam-v');sm.style.gridColumn=String(x-1);sm.style.gridRow=y+' / span '+H;page.append(sm)}
        page.append(b.el);x+=w+1});
      if(si){const sm=h('pg-seam pg-seam-h');sm.style.gridColumn='1 / -1';sm.style.gridRow=String(y-1);page.append(sm)}
      y+=H+1});
    pg.append(page);return page});
  const labels=pages.map((sh,pi)=>{const names=[];sh.forEach(items=>items.forEach(({b})=>{if(b.title&&names.indexOf(b.title)<0)names.push(b.title)}));return !names.length?'Page '+(pi+1):names.length>2?names[0]+' – '+names[names.length-1]:names.join(' · ')});
  return {el,pages:pages.length,labels,show:i=>els.forEach((p,k)=>p.classList.toggle('is-on',k===i))}}
function mkTabs(labels,pick,style,start){const t=h('pg-tabs');if(style)t.dataset.tabs=style;labels.forEach((l,i)=>{const b=h('pg-tab'+(i===(start||0)?' is-on':''),esc(l),'button');b.onclick=()=>{[...t.children].forEach((c,k)=>c.classList.toggle('is-on',k===i));pick(i)};t.append(b)});return t}

/* ---------- hero elements ---------- */
function hdr(ctx){const S=ctx.S;if(S.header_bar===false)return null;const el=h('pg-hdr pg-on-bg'),name=!S.title||S.title.size==='small',pres=S.presets||[S.preset||'Init','Preset 2','Preset 3'];let pi=0;
  el.innerHTML=(name&&S.title?'<span class="pg-hdr-name">'+esc(S.title.text)+'</span>':'')+'<span class="pg-hdr-sp"></span><span class="pg-preset"><button data-d="-1" title="Previous preset"><i></i></button><span>'+esc(pres[0])+'</span><button data-d="1" title="Next preset"><i></i></button></span><span class="pg-ab"><button class="is-on">A</button><button>B</button></span><button class="pg-byp is-on" title="Bypass"><i></i></button>';
  el.addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;if(b.dataset.d){pi=(pi+ +b.dataset.d+pres.length)%pres.length;el.querySelector('.pg-preset span').textContent=pres[pi]}
    else if(b.parentNode.classList.contains('pg-ab'))[...b.parentNode.children].forEach(c=>c.classList.toggle('is-on',c===b));
    else if(b.classList.contains('pg-byp')){b.classList.toggle('is-on');ctx.root.classList.toggle('is-bypassed',!b.classList.contains('is-on'))}});return el}
function badge(ctx){const b=ctx.S.badge;if(!b)return null;const el=b.src?h('pg-badge',null,'img'):h('pg-badge pg-ph','badge');if(b.src){el.src=b.src;el.alt=''}el.dataset.shape=b.shape||'free';return el}
function top(ctx){const S=ctx.S,t=S.title,b=badge(ctx),show=t&&!(S.header_bar!==false&&t.size==='small');if(!show&&!b)return null;const el=h('pg-top pg-legible');el.dataset.place=(t&&t.placement)||'top_left';if(el.dataset.place==='banner')el.classList.add('pg-on-accent');
  if(show){const x=h('pg-title',esc(t.text),'h1');x.dataset.size=t.size||'medium';el.append(x)}else el.append(h('pg-hdr-sp'));if(b)el.append(b);return el}
function foot(ctx){return h('pg-foot pg-legible','<span>Built by </span><b>'+esc(ctx.S.creator||'Pangea artist')+'</b><span> with Pangea</span>')}
function illus(ctx){const a=ctx.S.artwork||{},w=h('pg-illus-w');const el=a.src?h('pg-illus',null,'img'):h('pg-illus pg-ph','artwork<br>illustration');if(a.src){el.src=a.src;el.alt=''}el.dataset.shape=a.shape||'rect';w.append(el);w._fp=[168,152];return w}
function screws(el){['tl','tr','bl','br'].forEach((c,i)=>{const s=h('pg-screw');s.dataset.c=c;s.style.setProperty('--sr',[32,-18,71,-55][i]+'deg');el.append(s)})}
function art(ctx,host){const a=ctx.S.artwork;if(!a||!a.src||a.mode==='illustration'||!host)return;const im=h('pg-art',null,'img');im.src=a.src;im.alt='';host.prepend(im);host.classList.add('has-art')}

/* ---------- layout families ---------- */
const AUTO={display_first:n=>n<=5?'m':'s',flat_panel:n=>n<=6?'l':n<=14?'m':'s',hardware_unit:n=>n<=5?'l':n<=12?'m':'s',hero_knob:n=>n<=6?'m':'s',synth_page:n=>n<=8?'m':'s',poster:n=>n<=4?'l':n<=10?'m':'s',side_columns:n=>n<=16?'m':'s'/* Pangea patch */};
const FRAME={display_first:'rule',flat_panel:'rule',hardware_unit:'inset',hero_knob:'bracket',synth_page:'card',poster:'block',side_columns:'card'/* Pangea patch */};
const TABS={display_first:'',flat_panel:'',hardware_unit:'plate',hero_knob:'pill',synth_page:'pill',poster:'block',side_columns:'pill'/* Pangea patch */};
const up=s=>({s:'m',m:'l',l:'l'}[s]);
/* Pangea patch: spec.page_rows sets the rows of a page, the row budget flow and groupBlocks use: 46 when not given, as before. A family with a stage or a display above its controls keeps that share out of it, as before: pageRows(S,d) moves the family's own budget d by as much as page_rows moves from 46. */
const pageRows=(S,d)=>{const r=S.page_rows;return Number.isInteger(r)&&r>=24&&r<=240?d+r-46:d};
/* Pangea patch: spec.meters lists meters a host binds, each with an id; a section's controls may name one. isMeter says so; count() counts controls only. */
const isMeter=(S,id)=>(S.meters||[]).some(m=>m&&m.id===id);
const count=S=>S.sections.reduce((a,s)=>a+s.controls.filter(id=>!isMeter(S,id)).length,0);
function blocks(ctx,size,cols,maxRows,sk){const S=ctx.S,out=[],n=count(S),fr=S.group_style||FRAME[S.layout],ws=n>20?'s':'m';sk=sk||{};
  if(S.artwork&&S.artwork.mode==='illustration'&&!sk.illus)out.push(wb(ctx.illus(),null,'none'));
  if(S.display&&!sk.display)out.push(wb(ctx.display(ws),null,'none'));
  if(S.xy_pad&&!sk.xy)out.push(wb(ctx.xy(ws),null,'none'));
  if(!sk.macros)S.macros.forEach(m=>{if(m.control!==sk.hero)out.push(wb(ctx.macro(m,ws),null,'none'))});
  if(S.meter&&!sk.meter&&S.meter.type==='vu_needle')out.push(wb(ctx.meter(n>12?'m':'l'),null,fr==='inset'?'inset':'none'));
  /* Pangea patch: a meter named in a section is packed there beside the controls, with its own footprint; a listed meter no section names follows the last section. */
  S.sections.forEach(sec=>{const ids=sec.controls.filter(id=>id!==sk.hero);if(ids.length)groupBlocks(sec.group,ids.map(id=>{const el=isMeter(S,id)?ctx.meter(size(id),id):ctx.ctl(id,size(id));return {el,w:Math.ceil(el._fp[0]/8),h:Math.ceil(el._fp[1]/8)}}),cols,maxRows,fr).forEach(b=>out.push(b))});
  (S.meters||[]).forEach(m=>{if(m&&!S.sections.some(s=>s.controls.indexOf(m.id)>=0))out.push(wb(ctx.meter(ws,m.id),null,'none'))});
  if(S.meter&&!sk.meter&&S.meter.type!=='vu_needle')out.push(wb(ctx.meter(ws),null,'none'));
  if(S.step_grid&&!sk.steps)out.push(Object.assign(wb(ctx.steps(ws,cols-2),null,'none'),{full:true}));
  return out}
function flowInto(host,ctx,bl,cols,maxRows){const f=flow(bl,cols,maxRows);if(f.pages>1)host.append(mkTabs(f.labels,f.show,ctx.S.tab_style||TABS[ctx.S.layout]));host.append(f.el);return f}
const LAYOUT={
 display_first(ctx){const S=ctx.S,win=h('pg-win pg-on-bg'),n=count(S),sz=AUTO.display_first(n),hero=S.hero&&S.hero.control,side=h('pg-side'),stage=h('pg-stage');
  add(win,hdr(ctx),top(ctx));/* Pangea patch: no display in the spec, no display drawn: the made-up live spectrum moved with no sound behind it (decision 76). The stage keeps what its side holds, and is left out when that is nothing. */if(S.display)stage.append(ctx.display('l',true));else stage.classList.add('is-bare');
  if(S.xy_pad)side.append(fix(ctx.xy('l')));if(hero)side.append(fix(ctx.ctl(hero,'l','fader_v')));if(S.meter&&S.meter.type!=='vu_needle')side.append(fix(ctx.meter('l')));if(side.children.length)stage.append(side);if(stage.children.length)win.append(stage);
  if(S.step_grid){const st=h('pg-strip');st.append(fix(ctx.steps('s',108)));win.append(st)}
  const bar=h('pg-ctlbar pg-mat pg-on-face'),mr=pageRows(S,sz==='m'?17:14)/* Pangea patch: page_rows */;flowInto(bar,ctx,blocks(ctx,()=>sz,108,mr,{display:1,xy:1,steps:1,hero:hero,meter:S.meter&&S.meter.type!=='vu_needle'}),108,mr);screws(bar);win.append(bar,foot(ctx));ctx.mat=bar;return win},
 flat_panel(ctx){const S=ctx.S,win=h('pg-face pg-mat pg-on-face'),n=count(S),sz=AUTO.flat_panel(n),hero=S.hero&&S.hero.control,body=h('pg-body');
  add(win,top(ctx));const mr=pageRows(S,46)/* Pangea patch: page_rows */;flowInto(body,ctx,blocks(ctx,id=>id===hero?up(sz):sz,104,mr),104,mr);win.append(body,foot(ctx));screws(win);ctx.mat=win;return add(h('pg-win pg-on-bg'),hdr(ctx),win)},
 hardware_unit(ctx){const S=ctx.S,win=h('pg-win pg-on-bg'),n=count(S),sz=AUTO.hardware_unit(n),hero=S.hero&&S.hero.control,rw=h('pg-rackw'),rack=h('pg-rack pg-mat pg-on-face'),inn=h('pg-rack-in');
  const ear=()=>{const e=h('pg-ear');for(let i=0;i<2;i++){const s=h('pg-screw');s.style.setProperty('--sr',(i*67+25)+'deg');e.append(s,h('pg-slot'))}return e};
  add(win,hdr(ctx));rack.append(ear(),inn,ear());add(inn,top(ctx));const mr=pageRows(S,46)/* Pangea patch: page_rows */;flowInto(inn,ctx,blocks(ctx,id=>id===hero?up(sz):sz,98,mr),98,mr);inn.append(foot(ctx));rw.append(rack);win.append(rw);ctx.mat=rack;return win},
 hero_knob(ctx){const S=ctx.S,win=h('pg-face pg-mat pg-on-face'),hero=(S.hero&&S.hero.control)||(S.macros[0]&&S.macros[0].control)||/* Pangea patch: the first control, never a meter */S.sections.map(s=>s.controls.filter(id=>!isMeter(S,id))[0]).filter(id=>id!=null)[0],mm=S.macros.filter(m=>m.control===hero)[0],stage=h('pg-stage pg-legible'),body=h('pg-body');
  add(win,top(ctx));if(S.artwork&&S.artwork.mode==='illustration')stage.append(fix(ctx.illus()));if(S.display)stage.append(fix(ctx.display('s')));else if(S.xy_pad)stage.append(fix(ctx.xy('m')));
  stage.append(fix(mm?ctx.macro(mm,'l',true):ctx.ctl(hero,'l','hero')));if(S.meter)stage.append(fix(ctx.meter('m')));win.append(stage);
  const n=count(S)-(mm?0:1),sz=AUTO.hero_knob(n),mr=pageRows(S,sz==='m'?17:14)/* Pangea patch: page_rows */;flowInto(body,ctx,blocks(ctx,()=>sz,104,mr,{display:1,xy:1,meter:1,illus:1,hero:hero}),104,mr);win.append(body,foot(ctx));screws(win);ctx.mat=win;return add(h('pg-win pg-on-bg'),hdr(ctx),win)},
 synth_page(ctx){const S=ctx.S,win=h('pg-win pg-on-bg'),n=count(S),sz=AUTO.synth_page(n),st=h('pg-synth-top'),views=h('pg-views'),main=h('pg-view'),mv=h('pg-view');
  add(win,hdr(ctx));add(st,top(ctx));const mr=pageRows(S,50)/* Pangea patch: page_rows */,bl=blocks(ctx,()=>sz,109,mr);bl.forEach(b=>b.el.classList.add('pg-mat','pg-on-face'));const f=flow(bl,109,mr);main.append(f.el);views.append(main);
  const labels=f.pages>1?f.labels.slice():['Controls'];if(S.modulation){labels.push('Modulation');mv.append(modView(ctx));views.append(mv)}
  const start=S.modulation&&S.view==='modulation'?labels.length-1:0,pick=i=>{const m=S.modulation&&i===labels.length-1;main.classList.toggle('is-on',!m);mv.classList.toggle('is-on',!!m);if(!m)f.show(i)};
  if(labels.length>1)st.append(mkTabs(labels,pick,S.tab_style||TABS.synth_page,start));pick(start);win.append(st,views,foot(ctx));screws(win);return win},
 poster(ctx){const S=ctx.S,win=h('pg-win pg-on-bg'),n=count(S),sz=AUTO.poster(n),place=(S.title&&S.title.placement)||'top_left',tp=top(ctx),po=h('pg-po'),L=h('pg-po-l pg-on-accent'),R=h('pg-po-r pg-mat pg-on-face');
  add(win,hdr(ctx));if(tp){if(place==='top_left')L.append(tp);else win.append(tp)}
  if(S.artwork&&S.artwork.mode==='illustration')L.append(ctx.illus());if(S.xy_pad)L.append(fix(ctx.xy('m')));S.macros.forEach(m=>L.append(fix(ctx.macro(m,'m'))));
  const mr=pageRows(S,46)/* Pangea patch: page_rows */;flowInto(R,ctx,blocks(ctx,()=>sz,62,mr,{xy:1,macros:1,steps:1,illus:1}),62,mr);po.append(L,R);win.append(po);screws(R);
  if(S.step_grid){const sp=h('pg-strip pg-mat pg-on-face');sp.append(fix(ctx.steps('m',108)));win.append(sp)}
  win.append(foot(ctx));ctx.mat=R;return win},
 /* Pangea patch: side_columns, Pattern Player's Perform page (docs/felt-look.md). A section the spec gives a side (`side`: left or right, as a MIDI tool's two hands) stands in a column at that side, the left one in the highlight colour and the right one in the accent; in the middle, a stage of the XY pad and the chord map side by side and the notes under them, then the other sections in order; and along the foot a bar with the variation (S.generate) before the credit. Each column packs its own sections, with pages of its own only when it must. */
 side_columns(ctx){const S=ctx.S,win=h('pg-face pg-mat pg-on-face'),n=count(S),sz=AUTO.side_columns(n),cols=h('pg-cols'),mr=pageRows(S,62);add(win,top(ctx));
  const sideOf=s=>s.side==='left'||s.side==='right'?s.side:'centre',of=k=>S.sections.filter(s=>sideOf(s)===k),gen=S.generate||null;
  const part=k=>{const list=of(k).map(s=>({group:s.group,side:s.side,controls:s.controls.filter(id=>id!==gen)})).filter(s=>s.controls.length);return list.length?blocks(Object.assign(Object.create(ctx),{S:Object.assign({},S,{sections:list})}),()=>sz,k==='centre'?CW_C(of('left').length,of('right').length):SIDE_C,mr,{display:1,xy:1,steps:1,macros:1,illus:1,meter:1}):[]};
  /* each side 22 columns (176 units: four step bars), the middle the rest, less the gaps */
  const SIDE_C=22,CW_C=(l,r)=>104-(l?SIDE_C+2:0)-(r?SIDE_C+2:0);
  ['left','centre','right'].forEach(k=>{const bl=part(k),col=h('pg-col');col.dataset.side=k;
    if(k==='centre'){const st=h('pg-cstage'),top2=h('pg-cstage-t');if(S.xy_pad)top2.append(ctx.xy('s'));if(S.chord_map)top2.append(ctx.chordMap('s'));if(top2.children.length)st.append(top2);if(S.note_roll)st.append(ctx.noteRoll('s'));if(st.children.length)col.append(st)}
    const w=k==='centre'?CW_C(of('left').length,of('right').length):SIDE_C;col.style.setProperty('--cols',w);if(bl.length){const box=h('pg-col-flow');col.append(box);flowInto(box,ctx,bl,w,mr)}
    if(col.children.length||k==='centre')cols.append(col)});
  win.append(cols);const bar=h('pg-cfoot');if(gen){const g=ctx.ctl(gen,'m','generate');bar.append(g)}bar.append(foot(ctx));win.append(bar);ctx.mat=win;return add(h('pg-win pg-on-bg'),hdr(ctx),win)}
};
/* synth_page modulation view: sources with a shape display + their controls, and a list of fixed routings (source → target, amount). Never a patch bay. */
function modView(ctx){const M=ctx.S.modulation,el=h('pg-mod'),src=h('pg-mod-src'),rt=h('pg-group pg-mod-rt pg-mat pg-on-face');rt.dataset.frame='card';
  (M.sources||[]).forEach(s=>{const card=h('pg-group pg-mat pg-on-face');card.dataset.frame='card';card.append(h('pg-g-t','<span>'+esc(s.label)+'</span>'));const row=h('pg-mod-row');
    const d=display({kind:s.type==='lfo'?'lfo':s.type==='envelope'?'envelope':'curve',size:'s',label:s.type,live:true,handles:s.type==='envelope'});d.style.height='';d.style.width='';row.append(d);
    if(s.type==='envelope'&&s.bind){const B=s.bind;['a','d','s','r'].forEach(k=>{if(!B[k])return;d._d.env[k]=ctx.n(B[k]);ctx.sub(B[k],n=>{d._d.env[k]=n;d.draw()})});d.draw();d.onHandle=(i,st)=>{[['a'],['d','s'],['r']][i].forEach(k=>B[k]&&ctx.user(B[k],st.env[k]))}}
    if(s.type==='lfo'&&s.shape){d._d.shape=Math.round(ctx.n(s.shape)*3);ctx.sub(s.shape,n=>{d._d.shape=Math.round(n*(ctx.gp(s.shape).steps-1))})}
    if(s.type==='velocity'&&s.curve){ctx.sub(s.curve,n=>{d._d.curve=n;d.draw()})}
    (s.controls||[]).forEach(id=>row.append(fix(ctx.ctl(id,'s'))));card.append(row);src.append(card)});
  rt.append(h('pg-g-t','<span>Routings</span>'));rt.append(h('pg-rt pg-rt-h','<span>Source</span><i></i><span>Target</span><span>Amount</span><span></span>'));
  (M.routings||[]).forEach(r=>{const row=h('pg-rt','<span class="pg-rt-s">'+esc(r.source)+'</span><i class="pg-rt-ar"></i><span>'+esc(r.target)+'</span>'),c=ctx.ctl(r.amount,'s','fader_h'),p=ctx.gp(r.amount),v=h('pg-rt-v',fmt(p,toVal(p,ctx.n(r.amount))),'span');
    ctx.sub(r.amount,n=>{v.textContent=fmt(p,toVal(p,n))});row.append(c,v);rt.append(row)});
  el.append(src,rt);return el}

/* ---------- render a plugin from a spec ---------- */
function pick(p,S,a){if(a.as)return a.as;if(p.tempo)return 'tempo_sync';if(p.kind==='momentary')return 'lit_button';
  if(p.kind==='toggle')return S.switch_style==='segmented'?'segmented':S.switch_style;
  if(p.kind==='choice')return S.switch_style==='segmented'&&p.choices.length<=4?'segmented':S.selector_style==='tempo_sync'?'dropdown':S.selector_style;
  if(p.kind==='stepped')return S.selector_style==='number_box'?'number_box':'knob';return 'knob'}
/* Pangea patch: a rotary's position name may wrap onto two lines when it has room above and below: five positions or fewer, in the knob's upper half (rotary). rlChars is a name's widest line in characters, on one line or at its best split into two. */
const rlWrap=(N,a)=>N<=5&&Math.cos(a)>-.01;
const rlChars=(t,wrap)=>{const ws=String(t).trim().split(/\s+/);if(!wrap||ws.length<2)return String(t).length;let best=Infinity;for(let k=1;k<ws.length;k++)best=Math.min(best,Math.max(ws.slice(0,k).join(' ').length,ws.slice(k).join(' ').length));return best};
/* Pangea patch: the width a stepped_rotary needs for its position names (rotary): each name starts outside the knob at its position's angle and runs outward, or is centred above it, and the control is as wide as the furthest name on either side, the active name in bold. */
function rotaryNeed(p,size,cw){const N=(p.choices||[]).length;if(N<2||N>8)return 0;const d={s:36,m:54,l:80}[size]||54,ra=d/2+5;let half=0;
  p.choices.forEach((c,i)=>{const a=(-135+270*i/(N-1))*Math.PI/180,sx=Math.abs(Math.sin(a)),w=rlChars((p.short&&p.short[i])||c,rlWrap(N,a))*cw*1.05;half=Math.max(half,sx>.3?sx*ra+w:sx*ra+w/2)});return Math.ceil(2*half/8)*8}
/* Pangea patch: a label of two or more words wraps within its control less LGAP units, so its text keeps at least that gap from the next control's label: HIMID FREQ and HIMID GAIN read as one run of words when each filled its control and the six units beside it. The box is never narrower than the longest word, nor narrower than a label of three or more words needs to stay on two lines when it did before, nor wider than before. A one-word label is as before. */
const LGAP=8;
function fitLabel(el,label,word,cw){const ws=label.trim().split(/\s+/);if(ws.length<2)return;const W=el._fp[0];let two=0;
  if(ws.length>2){two=Infinity;for(let k=1;k<ws.length;k++)two=Math.min(two,Math.max(ws.slice(0,k).join(' ').length,ws.slice(k).join(' ').length)*cw);if(two>W+6)two=0}
  const box=Math.min(W+6,Math.max(W-LGAP,word,two&&two+6));if(box>=W+6)return;el._lbl.dataset.fit='';el._lbl.style.setProperty('--lb',box.toFixed(1))}
P.render=function(root,spec,params){if(root._ro)root._ro.disconnect();
  const S=Object.assign({layout:'flat_panel',material:'matte',variant:'dark',font:'grotesk_1',knob_style:'flat_ring',fader_style:'flat_line',switch_style:'toggle',selector_style:'dropdown',header_bar:true},spec);
  S.sections=S.sections||[];S.macros=S.macros||[];S.details=S.details||[];if(!LAYOUT[S.layout])S.layout='flat_panel';
  root.classList.add('pg','pg-plugin');root.dataset.layout=S.layout;root.dataset.material=S.material;root.dataset.variant=S.variant;root.dataset.details=S.details.join(' ');
  P.theme(root,S.palette||'dark',S.material,S.variant);P.font(root,S.font);
  const PM={},vals={},subs={},bases={},linked={},MEL={}/* Pangea patch: the listed meters drawn, by id */;(params||[]).forEach(d=>{const p=param(d);PM[p.id]=p});S.macros.forEach(m=>(m.targets||[]).forEach(t=>{linked[t]=1}));
  const gp=id=>PM[id]||(PM[id]=param(id)),fo=P.fonts[S.font]||P.fonts.grotesk_1,CW=(fo.group==='mono'?.6+parseFloat(fo.trk):{grotesk:.56,condensed:.46,serif:.54,display:.56}[fo.group]*(fo.case==='none'?1:1.22)+parseFloat(fo.trk))*fo.fs*13.5,
   /* Pangea patch: a character of a rotary's position names as written (not upper case, .02em apart), for rotaryNeed. */CWN=((fo.group==='mono'?.6:{grotesk:.56,condensed:.46,serif:.54,display:.56}[fo.group])+.02)*fo.fs*13.5;
  const ctx={S,root,gp,n:id=>vals[id]!=null?vals[id]:toN(gp(id),gp(id).default),
   set(id,n){vals[id]=n;(subs[id]||[]).forEach(f=>f(n));root.dispatchEvent(new CustomEvent('pangea:change',{detail:{id:id,value:toVal(gp(id),n)}}))},
   user(id,n){bases[id]=n;ctx.set(id,n)},sub(id,f){(subs[id]=subs[id]||[]).push(f)},
   ctl(id,size,force){const p=gp(id),a=(S.arrangement||{})[id]||{};let type=force||pick(p,S,a);size=(!force&&a.size)||size;const o={size,n:ctx.n(id),aria:p.aria/* Pangea patch */,cwn:CWN/* Pangea patch: the width of a position name's character, for rotary */};
     if(type==='knob')o.look=S.knob_style;else if(type==='fader'||type==='fader_v'||type==='fader_h')o.look=S.fader_style;else if(type==='stepped_rotary')o.knobLook=S.knob_style==='hero'?'flat_ring':S.knob_style;
     if(type==='segmented'&&p.kind!=='toggle'&&p.kind!=='choice')type='knob';if((type==='dropdown'||type==='stepped_rotary')&&p.kind!=='choice')type=p.kind==='toggle'?'toggle':'knob';
     const el=control(type,p,o);{const L=Math.max.apply(null,String(p.label).split(/[\s-]+/).map(w=>w.length))+(linked[id]?1.5:0),need=Math.max(Math.ceil((L*CW-6)/8)*8,type==='stepped_rotary'?rotaryNeed(p,size,CWN):0);if(need>el._fp[0])el._fp=[need,el._fp[1]];fitLabel(el,String(p.label),L*CW,CW)}if(linked[id])el._lbl.insertAdjacentHTML('afterbegin','<i class="pg-link"></i>');
     el.onInput=n=>ctx.user(id,n);ctx.sub(id,n=>el.setN(n,true));return el},
   macro(m,size,hero){const p=gp(m.control),el=macro(p,{size,hero,n:ctx.n(m.control),targets:(m.targets||[]).map(t=>gp(t).label)});el._fp=hero?[264,288]:SZ.macro[size];
     el.onInput=n=>{vals[m.control]=n;(m.targets||[]).forEach(t=>{const b=bases[t]!=null?bases[t]:toN(gp(t),gp(t).default);ctx.set(t,clamp(b+n*(1-b)*.85))})};return el},
   /* Pangea patch: ctx.meter(size,id) draws the listed meter with that id: bound to its host, so it starts at rest and never runs the demo animation, and it is kept so the host reaches it with ctx.meterOf(id). Its label widens it as a control's does. */
   meter(size,id){if(id==null){const m=S.meter;return meter({type:m.type,size,label:m.label||m.source,live:true})}
     const m=(S.meters||[]).filter(x=>x&&x.id===id)[0],el=meter({type:m.type,size:m.size||size,label:m.label,stereo:m.stereo===true,level:0});
     {const L=Math.max.apply(null,String(m.label||'').split(/[\s-]+/).map(w=>w.length)),need=Math.ceil((L*CW-6)/8)*8;if(need>el._fp[0])el._fp=[need,el._fp[1]]}MEL[id]=el;return el},
   meterOf(id){return MEL[id]||null},
   illus(){return illus(ctx)},
   /* Pangea patch: the chord map and the notes (docs/felt-look.md), kept so the host reaches them with ctx.parts. A press on a chord goes to the host's ctx.onChoose. */
   parts:{},
   chordMap(size){const el=chordMap({size,label:(S.chord_map&&S.chord_map.label)||'Chord map'});el.onChoose=S.chord_map&&S.chord_map.choose?(d,st)=>{if(ctx.onChoose)ctx.onChoose(d,st)}:null;ctx.parts.chord_map=el;return el},
   noteRoll(size){const el=noteRoll({size,label:(S.note_roll&&S.note_roll.label)||'Notes',note:(S.note_roll&&S.note_roll.note)||''});ctx.parts.note_roll=el;return el},
   xy(size){const X=S.xy_pad,px=gp(X.x),py=gp(X.y),el=xy({x:px,y:py,size,nx:ctx.n(X.x),ny:ctx.n(X.y)});el.onInput=(x,y,mx,my)=>{/* Pangea patch: only the axes that moved */if(mx!==false)ctx.user(X.x,x);if(my!==false)ctx.user(X.y,y)};ctx.sub(X.x,n=>el.setXY(n,el._y,true));ctx.sub(X.y,n=>el.setXY(el._x,n,true));return el},
   steps(size,cols){const G=S.step_grid;return steps({n:G.steps||16,size,label:G.label||'Steps',flags:G.flags||[],cols,live:true})},
   display(size,fill){const D=S.display,hs=D.handles,o={kind:D.type,size,label:D.label,spectrum:D.spectrum,live:true,handles:!!hs||!!D.bind};
     const fpos=(p,n)=>clamp(Math.log10(toVal(p,n)/20)/3),fn=(p,f)=>toN(p,clamp(20*Math.pow(1000,f),p.min,p.max));
     if(D.type==='filter_response'&&hs)o.bands=hs.map(x=>({f:fpos(gp(x.x),ctx.n(x.x)),g:x.y?ctx.n(x.y)*2-1:0,q:x.q?ctx.n(x.q):.5,type:x.type}));
     if(D.type==='envelope'&&D.bind){o.env={};['a','d','s','r'].forEach(k=>{if(D.bind[k])o.env[k]=ctx.n(D.bind[k])})}
     const el=display(o);if(fill)el._fp=null;
     if(D.type==='filter_response'&&hs){hs.forEach((x,i)=>{const b=el._d.bands[i];ctx.sub(x.x,n=>{b.f=fpos(gp(x.x),n);el.draw()});if(x.y)ctx.sub(x.y,n=>{b.g=n*2-1;el.draw()});if(x.q)ctx.sub(x.q,n=>{b.q=n;el.draw()})});
       el.onHandle=(i,st)=>{const x=hs[i],b=st.bands[i];ctx.user(x.x,fn(gp(x.x),b.f));if(x.y)ctx.user(x.y,(b.g+1)/2)}}
     if(D.type==='envelope'&&D.bind){const B=D.bind;['a','d','s','r'].forEach(k=>{if(B[k])ctx.sub(B[k],n=>{el._d.env[k]=n;el.draw()})});el.onHandle=(i,st)=>{[['a'],['d','s'],['r']][i].forEach(k=>B[k]&&ctx.user(B[k],st.env[k]))}}
     return el}};
  const win=LAYOUT[S.layout](ctx);root.textContent='';root.append(win);
  if(S.artwork&&S.artwork.src)art(ctx,S.artwork.mode==='background'?win:(ctx.mat||win));
  const size=w=>{if(w)root.style.setProperty('--u',(w/900)+'px')};size(root.clientWidth);
  if(typeof ResizeObserver!=='undefined'){root._ro=new ResizeObserver(e=>size(e[0].contentRect.width));root._ro.observe(root)}
  root._ctx=ctx;return ctx};

/* ---------- demo content for the documentation: generic specs at any control count ---------- */
const PDEF={Gain:{unit:'dB',min:-24,max:24,bipolar:true,default:0},Pad:{kind:'toggle'},Phase:{kind:'toggle'},Cutoff:{unit:'Hz',scale:'log',min:20,max:20000,default:1200},Slope:{kind:'choice',choices:['12','24','48']},'Env Amount':{bipolar:true,min:-1,max:1,default:.3},
 Low:{unit:'dB',min:-12,max:12,bipolar:true,default:3},Mid:{unit:'dB',min:-12,max:12,bipolar:true,default:-2},High:{unit:'dB',min:-12,max:12,bipolar:true,default:4},'Mid Freq':{unit:'Hz',scale:'log',min:200,max:8000,default:1000},Tilt:{bipolar:true,min:-1,max:1,default:0},
 Rate:{unit:'Hz',scale:'log',min:.05,max:20,default:1.2},Shape:{kind:'choice',choices:['Sine','Tri','Saw','Square']},Sync:{kind:'toggle'},Decay:{unit:'s',scale:'log',min:.2,max:20,default:2.4},'Pre-delay':{unit:'ms',min:0,max:200,default:30},
 Threshold:{unit:'dB',min:-60,max:0,default:-18},Ratio:{kind:'stepped',min:1,max:10,default:4},Attack:{unit:'ms',scale:'log',min:.1,max:200,default:10},Release:{unit:'ms',scale:'log',min:10,max:2000,default:120},Crush:{kind:'stepped',min:1,max:16,default:12,unit:'bit'},
 Mix:{unit:'%',max:100,default:50},Balance:{bipolar:true,min:-50,max:50,unit:'%',default:0},Limit:{kind:'toggle',default:1},Ceiling:{unit:'dB',min:-12,max:0,default:-.3},Level:{unit:'dB',min:-24,max:6,default:0},Drive:{unit:'dB',min:0,max:24,default:9}};
const POOL={Input:['Gain','Drive','Pad','Phase','Trim'],Filter:['Cutoff','Resonance','Slope','Key Track','Env Amount'],Tone:['Low','Mid','High','Mid Freq','Tilt'],Motion:['Rate','Depth','Shape','Sync','Spread'],Space:['Size','Decay','Pre-delay','Damping','Width'],Dynamics:['Threshold','Ratio','Attack','Release','Knee'],Character:['Age','Noise','Crush','Wobble','Flutter'],Output:['Mix','Balance','Limit','Ceiling','Level']};
const SETS={3:[['Tone',['Low','Mid','High']]],4:[['Input',['Gain','Drive']],['Output',['Mix','Level']]],12:[['Filter',['Cutoff','Resonance','Slope','Env Amount']],['Space',['Size','Decay','Pre-delay','Damping']],['Output',['Mix','Balance','Limit','Level']]],
 30:['Input','Filter','Tone','Space','Dynamics','Output'].map(k=>[k,POOL[k]]),40:Object.keys(POOL).map(k=>[k,POOL[k]])};
const FAM={
 display_first:{material:'matte',variant:'dark',font:'grotesk_3',palette:{bg:'#0e1014',panel:'#1a1e26',text:'#dbe2ec',accent:'#4fd1c5',highlight:'#f6ad55'},knob_style:'flat_ring',switch_style:'lit_button',display:{type:'filter_response',spectrum:true,label:'Response'},title:{text:'Display First',size:'small',placement:'top_left'}},
 flat_panel:{material:'matte',variant:'dark',font:'condensed_2',palette:{bg:'#8f2a17',panel:'#b8371f',text:'#fff1dc',accent:'#ffd447',highlight:'#1d1b33'},knob_style:'flat_ring',switch_style:'lit_button',title:{text:'Flat Panel',size:'large',placement:'top_left'}},
 hardware_unit:{material:'brushed_metal',variant:'light',details:['screws','seams'],font:'condensed_3',palette:{bg:'#0f1113',panel:'#b9bcc0',text:'#17191c',accent:'#d0342c',highlight:'#e9a100'},knob_style:'metal_cap',fader_style:'channel_strip',switch_style:'toggle',meter:{type:'vu_needle',source:'Output'},title:{text:'Hardware Unit',size:'medium',placement:'top_left'}},
 hero_knob:{material:'painted_metal',variant:'light',font:'grotesk_2',palette:{bg:'#1f2a2e',panel:'#d7e0d6',text:'#1a2a2a',accent:'#e0522d',highlight:'#2c6e63'},knob_style:'metal_cap',switch_style:'rocker',title:{text:'Hero Knob',size:'medium',placement:'top_centre'}},
 synth_page:{material:'matte',variant:'dark',font:'mono_4',palette:{bg:'#121019',panel:'#221f2e',text:'#e6e1f5',accent:'#9d7bff',highlight:'#5fe3c0'},knob_style:'flat_ring',switch_style:'lit_button',title:{text:'Synth Page',size:'small',placement:'top_left'}},
 poster:{material:'paper',variant:'light',font:'display_2',palette:{bg:'#f4ead2',panel:'#fbf5e6',text:'#161616',accent:'#ff4f1f',highlight:'#1947e5'},knob_style:'flat_ring',switch_style:'lit_button',title:{text:'Poster',size:'large',placement:'top_left'}}};
P.demoSpec=function(layout,n){const set=SETS[n]||SETS[12],spec=Object.assign({layout,creator:'Demo',sections:set.map(s=>({group:s[0],controls:s[1].slice()}))},FAM[layout]);
  if(layout==='hero_knob')spec.hero={control:set[0][1][n===4?1:0]};const params=[];set.forEach(s=>s[1].forEach(id=>params.push(Object.assign({id},PDEF[id]||{}))));return {spec,params}};

function demo(type,look,variant,size,state,x){x=x||{};variant=variant||'';const o={size,state:state==='default'?null:state,look};let p,el,key;
  switch(type){
   case 'knob':p=variant==='bipolar'?param({id:'Pan',bipolar:true,min:-50,max:50,unit:'%'}):variant==='stepped'?param({id:'Voices',kind:'stepped',min:1,max:8,default:4}):param({id:'Drive',unit:'dB',min:0,max:24});o.n=variant==='bipolar'?.72:variant==='stepped'?4/7:.62;el=knob(p,o);key=look==='hero'?'hero':'knob';break;
   case 'fader':{const hz=/horizontal/.test(variant),bi=/bipolar/.test(variant);p=bi?param({id:'Pan',bipolar:true,min:-50,max:50,unit:'%'}):param({id:'Level',unit:'dB',min:-60,max:6});o.n=bi?.28:.7;el=fader(p,o,hz?'h':'v');key=hz?'fader_h':'fader_v';break}
   case 'switch':if(look==='segmented'){p=param({id:'Mode',kind:'choice',choices:variant==='2 modes'?['A','B']:['I','II','I+II']});o.n=variant==='2 modes'?0:.5;el=seg(p,o)}
     else{const mom=/momentary/.test(variant);p=param({id:mom?'Tap':'Bypass',kind:mom?'momentary':'toggle'});o.n=/on$|held/.test(variant)?1:0;el=sw(p,o,look)}key=look;break;
   case 'selector':if(look==='dropdown'){p=param({id:'Wave',kind:'choice',choices:['Sine','Triangle','Saw','Square','Noise']});o.n=.5;el=dd(p,o)}
     else if(look==='stepped_rotary'){p=param({id:'Wave',kind:'choice',choices:variant==='6 positions'?['Sin','Tri','Saw','Sqr','Pls','Nse']:['Sin','Tri','Saw','Sqr']});o.n=variant==='6 positions'?.4:1/3;o.knobLook=x.knobLook||'chicken_head';el=rotary(p,o)}
     else if(look==='number_box'){p=variant==='continuous'?param({id:'Fine',unit:'st',min:-12,max:12,bipolar:true,default:2.5}):param({id:'Octave',kind:'stepped',min:-3,max:3,default:1});el=nb(p,o)}
     else{p=param({id:'Time',kind:'choice',tempo:true,choices:DIV,sync:variant!=='free',mod:variant==='dotted'?1:0});o.n=variant==='free'?.55:.6;el=tempo(p,o)}key=look;break;
   case 'meter':{const lv=parseFloat((variant.match(/\d+/)||[60])[0])/100;el=meter({type:look,size,state:o.state,level:lv,live:variant==='live'});break}
   case 'display':el=display({kind:look,size,state:o.state,handles:/handles/.test(variant),live:true,spectrum:look==='filter_response'});break;
   case 'xy_pad':el=xy({x:param({id:'Cutoff',unit:'Hz',scale:'log',min:20,max:20000,default:900}),y:param({id:'Resonance',unit:'%',max:100,default:62}),size,state:o.state});break;
   case 'macro':p=param({id:'Chaos',unit:'%',max:100});o.n=.58;o.targets=['Feedback','Drive','Wobble'];el=macro(p,o);key='macro';break;
   case 'step_grid':{const n=parseInt(variant)||16;el=steps({n,size,state:o.state,flags:size==='s'?[]:['accent','slide'],cols:n===8?30:56,live:o.state!=='disabled',label:'Pattern'});break}
   default:return h('')}
  if(!el._fp)el._fp=SZ[key][size];return el}
P.demo=demo;
const MX={knob:{rows:['normal','bipolar','stepped']},fader:{rows:['vertical','vertical bipolar','horizontal','horizontal bipolar']},
 switch:{toggle:['off','on'],rocker:['off','on'],lit_button:['latching off','latching on','momentary','momentary held'],segmented:['3 modes','2 modes']},
 selector:{dropdown:['choice'],stepped_rotary:['4 positions','6 positions'],number_box:['stepped','continuous'],tempo_sync:['synced','dotted','free']},
 meter:{rows:['level 12','level 50','level 82','level 100','live'],states:['default']},display:{waveform:['read-only live'],spectrum:['read-only live'],envelope:['read-only','with handles'],filter_response:['read-only live','with handles']},
 xy_pad:{rows:['two parameters']},macro:{rows:['drives three controls']},step_grid:{rows:['8 steps','16 steps','32 steps'],states:['default','disabled']}};
P.matrix=function(host){const d=host.dataset,type=d.type,look=d.look;host.textContent='';
  if(type==='group'){const row=h('pgx-row');['none','rule','card','inset','bracket','block'].forEach(fr=>{const items=['Attack','Decay','Sustain'].map((id,i)=>{const el=knob(param({id,unit:'ms',max:500}),{size:'s',look:d.knob||'flat_ring',n:.3+i*.2});return {el,w:8,h:9}});
      const g=mkGroup('Envelope',[{w:24,h:9,items}],fr);g.place(g.w);g.el.style.width='calc('+g.w*8+'*var(--u))';g.el.style.height='calc('+g.h*8+'*var(--u))';const c=h('pgx-cell');c.append(g.el,h('pgx-cap',fr));row.append(c)});host.append(row);return}
  if(type==='tabs'){const row=h('pgx-row');['','pill','plate','block'].forEach(s=>{const t=h('pg-tabs');if(s)t.dataset.tabs=s;['default','hover','active','disabled'].forEach(st=>{const b=h('pg-tab',st,'button');b.dataset.state=st;t.append(b)});const c=h('pgx-cell');c.style.alignItems='flex-start';c.append(t,h('pgx-cap',s||'underline'));row.append(c)});host.append(row);return}
  const cfg=MX[type]||{},rows=d.rows?d.rows.split(','):(cfg[look]||cfg.rows||['']),sizes=(d.sizes||'s,m,l').split(',');
  rows.forEach(v=>{const row=h('pgx-row');row.append(h('pgx-rowcap',esc((look?look+' · ':'')+v)));let states=d.states?d.states.split(','):cfg.states||['default','hover','active','disabled'];
    if(type==='display'&&!/handles/.test(v))states=['default','disabled'];
    sizes.forEach(sz=>states.forEach(st=>{const el=fix(demo(type,look,v,sz,st,d)),c=h('pgx-cell');if(look==='dropdown'&&st==='active')c.classList.add('pgx-open');c.append(el,h('pgx-cap',sz.toUpperCase()+(states.length>1?' · '+st:'')));row.append(c)}));host.append(row)})};

/* ---------- documentation mounting: fills <div data-pg="plugin|scope|matrix|ctl" data-…> placeholders. A plugin host calls Pangea.render directly. ---------- */
const camel=k=>k.replace(/_(.)/g,(m,c)=>c.toUpperCase());
const KEYS=['layout','material','variant','font','knob_style','fader_style','switch_style','selector_style','view','group_style','tab_style','creator'];
const DRAW={
 plugin(el){const d=el.dataset;let ex;if(d.demo){const a=d.demo.split(':');ex=P.demoSpec(a[0],+a[1])}else ex=(window.PangeaExamples||{})[d.example];
  if(!ex){el._tries=(el._tries||0)+1;if(el._tries<40)setTimeout(()=>{if(el.isConnected){el._pgSig=null;drawOne(el)}},150);return false}
  const spec=Object.assign({},ex.spec);KEYS.forEach(k=>{const v=d[camel(k)];if(v)spec[k]=v});
  if(d.palette)spec.palette=d.palette;if(d.details)spec.details=d.details!=='none'?d.details.split(/[ ,]+/):[];if(d.header)spec.header_bar=d.header!=='0';
  if(d.title||d.titleSize||d.titlePlace)spec.title=Object.assign({},spec.title,d.title?{text:d.title}:{},d.titleSize?{size:d.titleSize}:{},d.titlePlace?{placement:d.titlePlace}:{});
  if(d.artwork){const a=d.artwork.split(':');spec.artwork=a[0]==='none'?null:{mode:a[0],shape:a[1],src:d.artSrc||''}}
  if(d.badge)spec.badge=d.badge==='none'?null:{shape:d.badge,src:d.badgeSrc||''};
  let r=el.firstElementChild;if(!r){r=document.createElement('div');el.append(r)}P.render(r,spec,ex.params)},
 scope(el){const d=el.dataset;el.classList.add('pg');if(!d.material)d.material='matte';if(!d.variant)d.variant='dark';P.theme(el,d.palette||'dark',d.material,d.variant);P.font(el,d.font||'grotesk_1');
  const bg=d.surface==='bg';el.classList.toggle('pg-mat',!bg);el.classList.toggle('pg-on-bg',bg);if(bg)el.style.background='var(--bg)'},
 ctl(el){const d=el.dataset;el.textContent='';el.append(fix(demo(d.type,d.look,d.variant,d.size||'m',d.state||'default',d)))},
 matrix(el){P.matrix(el)}};
function drawOne(el){const f=DRAW[el.dataset.pg];if(!f)return;const sig=JSON.stringify(el.dataset);if(el._pgSig===sig)return;
  try{if(f(el)!==false){el._pgSig=JSON.stringify(el.dataset);el.style.minHeight=''}}catch(e){console.error('Pangea.mount',el.dataset.pg,e)}}
/* plugins and matrices are drawn when they come near the viewport, so a long page stays light */
const pend=new Set();let pt=0;
function check(){const H=window.innerHeight||800;pend.forEach(el=>{if(!el.isConnected){pend.delete(el);el._pgObs=0;return}const r=el.getBoundingClientRect();if(r.bottom>-900&&r.top<H+900){pend.delete(el);el._pgSeen=1;drawOne(el)}});if(!pend.size&&pt){clearInterval(pt);pt=0}}
P.mount=function(root){if(!root||!root.querySelectorAll)return;root.querySelectorAll('[data-pg]').forEach(el=>{const k=el.dataset.pg;
  if((k==='matrix'||k==='plugin')&&!el._pgSeen){if(!el._pgObs){el._pgObs=1;el.style.minHeight='140px';pend.add(el)}return}drawOne(el)});check();if(pend.size&&!pt)pt=setInterval(check,300)};
if(typeof window!=='undefined'&&window.addEventListener){window.addEventListener('scroll',check,{passive:true,capture:true});window.addEventListener('resize',check)}
})();
