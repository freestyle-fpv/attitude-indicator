(function(){
 'use strict';
 const $=id=>document.getElementById(id);
 const slides=Array.from(document.querySelectorAll('.dial-slide'));
 const dots=Array.from(document.querySelectorAll('[data-dial]'));
 const panel=document.querySelector('.showcase');
 const reduced=window.matchMedia('(prefers-reduced-motion: reduce)');
 let index=0,paused=reduced.matches,timer=null,hover=false,focused=false;
 function show(next){
   index=(next+slides.length)%slides.length;
   slides.forEach((slide,i)=>{slide.hidden=i!==index;});
   dots.forEach((dot,i)=>dot.setAttribute('aria-pressed',String(i===index)));
   $('dialTitle').textContent=slides[index].dataset.title;
   $('dialSubtitle').textContent=slides[index].dataset.subtitle;
   $('dialCounter').textContent=String(index+1).padStart(2,'0')+' / 04';
 }
 function auto(){
   clearInterval(timer);timer=null;
   $('carouselToggle').textContent=paused?'自动轮播':'暂停轮播';
   $('carouselToggle').setAttribute('aria-label',paused?'开始自动轮播':'暂停自动轮播');
   if(!paused&&!hover&&!focused&&!document.hidden&&!$('installPage').hidden){timer=setInterval(()=>show(index+1),4200);}
 }
 function manual(next){show(next);auto();}
 $('dialPrevious').addEventListener('click',()=>manual(index-1));
 $('dialNext').addEventListener('click',()=>manual(index+1));
 dots.forEach((dot,i)=>dot.addEventListener('click',()=>manual(i)));
 $('carouselToggle').addEventListener('click',()=>{paused=!paused;auto();});
 panel.addEventListener('mouseenter',()=>{hover=true;auto();});
 panel.addEventListener('mouseleave',()=>{hover=false;auto();});
 panel.addEventListener('focusin',()=>{focused=true;auto();});
 panel.addEventListener('focusout',event=>{if(!panel.contains(event.relatedTarget)){focused=false;auto();}});
 panel.addEventListener('keydown',event=>{
   if(event.key==='ArrowLeft'||event.key==='ArrowRight'){event.preventDefault();manual(index+(event.key==='ArrowRight'?1:-1));}
 });
 document.addEventListener('visibilitychange',auto);
 reduced.addEventListener('change',event=>{paused=event.matches;auto();});
 function page(){
   const diagnostics=location.hash==='#diagnostics';
   $('installPage').hidden=diagnostics;$('diagnosticsPage').hidden=!diagnostics;
   document.querySelectorAll('.nav a').forEach(link=>{
     if(link.hash===(diagnostics?'#diagnostics':'#install'))link.setAttribute('aria-current','page');
     else link.removeAttribute('aria-current');
   });
   document.title=(diagnostics?'设备诊断':'安装器')+' · 航模座舱仪表';
   auto();
 }
 window.addEventListener('hashchange',page);
 page();
})();
