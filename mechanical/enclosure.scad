// Revision 5: acrylic slide retention; no buttons; 3 parts. Millimeters.
part="layout";
$fn=48; eps=.03;
W=81; H=156; D=17.2; lid_t=1.6; wall=1.6;
DX=6.4; DY=17; DW=69; DH=97;
glass_z=1.9; device_t=14; acrylic_t=3;
acrylic_z=glass_z+device_t; acrylic_front=acrylic_z-acrylic_t;
seat_z=acrylic_z+.05; rail_shoulder=acrylic_front-.2;
// The acrylic slide grooves provide the forward stop; the window does NOT
// overlap or clamp glass. 0.2 mm reveal per side exposes the entire glass.
window_overlap=-.2; lateral_clearance=.2;
// Same latch projection reference as revision 4: +0.4 mm toward the outside.
// Stem reach from lid inner face: 12.3 - 0.7 = 11.6 mm.
hook_tip=D-11.6; hook_shoulder=hook_tip+2.6;
clips=[[0,8],[0,148],[1,28],[1,148]];
screen_setback=10; mount_angle=17;
mount_x=W+34; mount_z=44.8+(glass_z-2.2)-screen_setback;
bracket_span=mount_x+9*cos(mount_angle);
bracket_top_z=mount_z+9*sin(mount_angle)+3*cos(mount_angle);
usb_center_z=glass_z+20-screen_setback; usb_floor=usb_center_z-5.2;
module rr(w,h,r=1){hull()for(x=[r,w-r],y=[r,h-r])translate([x,y])circle(r=r);}
module slab(w,h,t,r=1){linear_extrude(t)rr(w,h,r);}
module bore(x,y,z,d,h){translate([x,y,z])cylinder(d=d,h=h);}
module mirrored(){translate([W,0,0])mirror([1,0,0])children();}
module front_locators(){
 // Short stops locate the glass perimeter, outside its 69 x 97 envelope.
 for(y=[DY+8,DY+81]){
  translate([DX-1.05,y,wall-eps])cube([.85,8,1.65]);
  translate([DX+DW+.2,y,wall-eps])cube([.85,8,1.65]);
 }
 // Lower stops close the acrylic drawer only when the two halves are assembled.
 // Both are clear of the USB-A body at the opposite end of the footer.
 for(x=[DX+6,DX+27])translate([x,DY-1.4,wall-eps])cube([6,1.2,14.2-wall+eps]);
 // Upper glass stops avoid the central USB-C connector.
 for(x=[DX+3,DX+DW-9])translate([x,DY+DH+.2,wall-eps])cube([6,1,1.65]);
}
module shell_u(){
 difference(){
  union(){
   difference(){slab(W,H,D,3);translate([wall,wall,wall])slab(W-2*wall,H-2*wall,D+1,1.4);}
   front_locators();
   translate([49,1,wall-eps])cube([W-wall-49,15.8,usb_floor-wall+eps]);
  }
  translate([DX+window_overlap,DY+window_overlap,-1])cube([DW-2*window_overlap,DH-2*window_overlap,wall+2]);
  for(c=clips)translate([c[0]==0?-1:W-wall-.1,c[1]-4.4,D-12.6])cube([wall+2,8.8,4.2]);
  translate([W-wall-.5,1.6,usb_floor-.2])cube([wall+2.5,14.8,D+2]);
  for(y=[.2,15])translate([64,y,-1])cube([3,1.8,usb_floor+3]);
 }
}
module left_clip_u(y){
 translate([y>100?.05:0,y,0])union(){
  translate([2.25,-4,hook_tip])cube([1,8,D-hook_tip+eps]);
  hull(){translate([2.25,-4,D-2])cube([1,8,.2]);translate([2.25,-4,D-eps])cube([1.8,8,.2]);}
  translate([0,-4,0])rotate([-90,0,0])linear_extrude(8)
   polygon([[2.3,-hook_shoulder],[.8,-hook_shoulder],[2.3,-hook_tip]]);
 }
}
module acrylic_left_rail(y){
 // Rigid, open-ended slide groove. Device slides along Y, not through the lip.
 translate([0,y+8,0])rotate([90,0,0])linear_extrude(8)
  polygon([[DX-1.25,D+eps],[DX-.2,D+eps],[DX-.2,rail_shoulder],
           [DX+.6,rail_shoulder],[DX-.2,rail_shoulder-.8],[DX-1.25,rail_shoulder-.8]]);
 // Pad supports acrylic rear, independently of the screen glass.
 translate([DX+12,y,seat_z])cube([5,8,D-seat_z+eps]);
}
module acrylic_seats(){
 for(y=[DY+16,DY+73]){
  acrylic_left_rail(y);
  translate([2*DX+DW,0,0])mirror([1,0,0])acrylic_left_rail(y);
 }
 // Closed upper end; lower end is closed by front-shell stops after assembly.
 for(x=[DX+4,DX+DW-10])translate([x,DY+DH+.2,rail_shoulder])cube([6,1.2,D-rail_shoulder+eps]);
}
module cover_u(){
 difference(){
  union(){
   translate([0,0,D])slab(W,H,lid_t,3);
   difference(){
    translate([1.95,1.95,D-1])slab(W-3.9,H-3.9,1+eps,.8);
    translate([3.05,3.05,D-2])cube([W-6.1,H-6.1,4]);
    translate([4.8,-1,D-2])cube([W,20,5]);
   }
   for(c=clips)if(c[0]==0)left_clip_u(c[1]);else translate([W,0,0])mirror([1,0,0])left_clip_u(c[1]);
   acrylic_seats();
   // Rear support pads avoid the sliding paths of the four original screw heads.
  }
  translate([W-12.2,39.8,D+lid_t-.6])cube([3.4,56.4,1.2]);
  translate([W-6.2,39.8,D+lid_t-.6])cube([2.4,56.4,1.2]);
  // Only strap holes remain in the free cable chamber; no race-track or fences.
  for(x=[9,W-11])translate([x,133,D-1])cube([2,6,4]);
 }
}
module pad_profile(){translate([mount_x,mount_z])rotate(mount_angle)difference(){
 translate([-9,0])square([18,3]);translate([-4,-.1])square([8,1.1]);}}
module bracket_profile(){union(){
 translate([W-14,D+lid_t+.1])square([14,3]);
 translate([W-12,D+lid_t-.4])square([3,.53]);
 translate([W-6,D+lid_t-.4])square([2,.53]);
 hull(){translate([W-4,D+lid_t+1])square([4,2.1]);translate([mount_x,mount_z])rotate(mount_angle)translate([-7.8,1.2])square([4,1.6]);}
 pad_profile();}}
module bracket_u(){translate([0,96,0])rotate([90,0,0])linear_extrude(56)bracket_profile();}
module bracket(){translate([bracket_span,bracket_top_z,-40])rotate([90,0,0])mirror([1,0,0])bracket_u();}
module shell(){mirrored()shell_u();}
module cover(){translate([W,H,D+lid_t])rotate([180,0,0])mirror([1,0,0])cover_u();}
module cable_reference_u(){
 pts=concat([[3.8,132,8.9],[3.8,18,8.9]],
 [for(a=[180:6:270])[11.8+8*cos(a),18+8*sin(a),8.9]],[[30,10,10.5],[47,9,usb_center_z],[51,9,usb_center_z]]);
 for(i=[0:len(pts)-2])hull(){translate(pts[i])sphere(d=4);translate(pts[i+1])sphere(d=4);}
}
module top_connector_reference_u(){
 // Unmeasured envelope using the user's ~30 mm protrusion; no fixed cable mandrel.
 translate([DX+DW/2-4.5,DY+DH+eps,3.9])cube([9,30-eps,10]);
 pts=concat([for(a=[0:6:90])[DX+DW/2-8+8*cos(a),DY+DH+30+8*sin(a),8.9]],
 [[14,152,8.9],[8,140,8.9],[3.8,132,8.9]]);
 for(i=[0:len(pts)-2])hull(){translate(pts[i])sphere(d=4);translate(pts[i+1])sphere(d=4);}
}
module usb_reference_u(){
 translate([W-23,2,usb_center_z-5])cube([23,14,10]);
 translate([W-31,6,usb_center_z-2])cube([8.03,6,4]);
 translate([W,3,usb_center_z-2.25])cube([12,12,4.5]);
}
module device_envelope_u(){
 // Conservative front/PCB slab and inset electronics; edge space adjacent to
 // acrylic is evidenced by user's side photo, not a vendor mechanical drawing.
 translate([DX,DY,glass_z])cube([DW,DH,6]);
 translate([DX+1.2,DY+1.2,glass_z+6])cube([DW-2.4,DH-2.4,device_t-6]);
 acrylic_u();
 for(x=[DX+4,DX+DW-4],y=[DY+4,DY+DH-4]){
  bore(x,y,glass_z+1.4,6,device_t-1.4);
  // Reference head allowance; actual screw dimensions remain user supplied.
  bore(x,y,acrylic_z,6.8,1);
 }
}
module acrylic_u(){translate([DX,DY,acrylic_front])cube([DW,DH,acrylic_t]);}
module device_u(){
 color([.15,.20,.22])translate([DX,DY,glass_z])cube([DW,DH,1.4]);
 // Display face is an illustrative inset, not a claimed measurement of the active region.
 color([.8,.84,.75])translate([DX+2,DY+3,glass_z-.05])cube([DW-4,DH-6,.1]);
 color([.74,.64,.47])acrylic_u();
 for(x=[DX+4,DX+DW-4],y=[DY+4,DY+DH-4])color([.6,.6,.62])bore(x,y,glass_z+1.4,5,device_t-acrylic_t-1.4);
}
module assembly(ex=0,device=true){
 color([.23,.30,.34])shell();
 color([.23,.63,.57])translate([0,0,ex])mirrored()cover_u();
 color([.64,.47,.28])translate([0,0,ex*1.6])mirrored()bracket_u();
 if(device)mirrored()device_u();
}
module layout(){translate([6,10,0])shell();translate([99,10,0])cover();translate([99,184,0])bracket();}
if(part=="shell")shell();
else if(part=="cover")cover();
else if(part=="bracket")bracket();
else if(part=="layout")layout();
else if(part=="assembly")assembly();
else if(part=="exploded")assembly(32,true);
else if(part=="interference")intersection(){shell_u();cover_u();}
else if(part=="device_check")intersection(){union(){shell_u();cover_u();}device_envelope_u();}
else if(part=="glue_interference")intersection(){cover_u();bracket_u();}
else if(part=="routing_check")intersection(){union(){shell_u();cover_u();device_envelope_u();}union(){cable_reference_u();usb_reference_u();top_connector_reference_u();}}
else if(part=="slide_check")intersection(){cover_u();union(){
 translate([DX,DY-130,glass_z])cube([DW,DH+130,6]);
 translate([DX+1.2,DY+1.2-130,glass_z+6])cube([DW-2.4,DH-2.4+130,device_t-6]);
 translate([DX,DY-130,acrylic_front])cube([DW,DH+130,acrylic_t]);
 for(x=[DX+4,DX+DW-4]){
  translate([x-3,DY-130+1,glass_z+1.4])cube([6,DH+124,device_t-1.4]);
  translate([x-3.4,DY-130+.6,acrylic_z])cube([6.8,DH+128.8,1]);
 }
}}
else assert(false,"Unknown part");
