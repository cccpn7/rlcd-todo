// Original monochrome fish, inspired by broad tropical-fish silhouettes.
// Rasterized once on a tiny pixel grid; no external image or color dependency.
function inside(x,y,p){let hit=false;for(let i=0,j=p.length-1;i<p.length;j=i++){const a=p[i],b=p[j];if((a[1]>y)!==(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])hit=!hit;}return hit;}
function sprite(w,h,body,extras,pattern,eye){
 const shapes=[body,...extras],filled=(x,y)=>shapes.some(p=>inside(x+.5,y+.5,p));
 return Array.from({length:h},(_,y)=>Array.from({length:w},(_,x)=>{
  if(!filled(x,y))return '.';
  if([[1,0],[-1,0],[0,1],[0,-1]].some(([dx,dy])=>!filled(x+dx,y+dy)))return '#';
  if(x===eye[0]&&y===eye[1])return '#';
  if(inside(x+.5,y+.5,body)&&x<eye[0]-2&&pattern(x,y))return '#';
  return 'w';
 }).join(''));
}
export const fishKinds=[
 {name:'梭形鱼',art:sprite(21,11,[[5,5],[10,2],[16,3],[20,6],[16,9],[9,9],[5,7]],[[[0,2],[6,5],[6,7],[0,10],[2,6]],[[8,3],[11,0],[15,3]]],(x,y)=>y===5&&x%3===0,[17,5])},
 {name:'圆背鱼',art:sprite(18,13,[[4,6],[7,2],[12,1],[16,4],[18,7],[15,11],[8,12],[4,8]],[[[0,4],[5,6],[5,8],[0,11]],[[7,3],[8,0],[13,0],[15,3]]],(x,y)=>(x+y)%4===0,[15,5])},
 {name:'神仙鱼',art:sprite(17,17,[[4,8],[9,3],[13,5],[17,9],[12,12],[8,13],[4,10]],[[[0,6],[5,8],[5,10],[0,12]],[[7,5],[8,0],[12,4]],[[8,11],[13,11],[10,17]]],x=>x===8||x===9||x===12,[14,8])},
 {name:'条纹鱼',art:sprite(19,12,[[4,5],[7,2],[13,2],[18,5],[19,7],[15,10],[7,10],[4,8]],[[[0,2],[5,5],[5,8],[0,11]],[[7,3],[8,0],[11,2]],[[9,9],[12,12],[14,9]]],x=>x%5<2,[16,5])},
 {name:'长尾鱼',art:sprite(23,10,[[7,4],[12,2],[18,3],[23,5],[20,7],[12,8],[7,6]],[[[0,2],[8,4],[8,6],[0,8],[2,5]],[[14,7],[18,7],[13,10]]],(x,y)=>y===4||x===12,[20,4])},
 {name:'扇尾鱼',art:sprite(19,13,[[5,5],[9,2],[14,3],[19,6],[16,9],[9,10],[5,8]],[[[0,1],[6,5],[6,8],[0,12]],[[8,3],[11,0],[14,3]],[[10,9],[12,12],[15,9]]],(x,y)=>x%4===0&&y%3===0,[16,5])},
];
