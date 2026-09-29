"""DeskPet cat enclosure V01. Units: mm. CadQuery 2.6.1.

Assembly: x right, y up, z toward viewer. Front exterior z=0.
Source board is Waveshare's official 2024-06-06 STEP, front glass z=3.8.
This is a first fit prototype, not a claim of physical fit verification.
"""
from pathlib import Path
import json
import math
import cadquery as cq
import trimesh

ROOT=Path(__file__).resolve().parent
for name in ('print','assembly','step','preview'):
    (ROOT/name).mkdir(exist_ok=True)

# Editable mechanical parameters. Do not scale the STL to adjust fit.
P={
    'glass_width':33.13,'glass_height':41.13,
    'glass_center_raw_y':-1.273,'glass_front_raw_z':3.8,
    'glass_recess':0.8,'radial_clearance':0.35,
    'body_width':42.4,'body_height':50.4,'body_radius':8.0,
    'body_depth':13.0,'lid_thickness':1.8,
    'window_overlap':0.7,'screw_pilot_diameter':1.7,
    'screw_clearance_diameter':2.3,'screw_head_diameter':4.5,
    'screw_positions':[[-18.9,16.0],[18.9,16.0],[-18.9,-16.0],[18.9,-16.0]],
    'board_pad_positions':[[-12.8,14.5,0.8],[11.5,16.4,1.2],[-11.5,-17.0,1.0],[11.5,-16.4,1.2]],
    'pad_top_z':-7.55,'soft_pad_nominal_thickness':0.5,
    'usb_center_y':0.258,'usb_center_z':-8.37,
    'usb_opening_y':13.0,'usb_opening_z':7.0,
    'button_y':[9.3381,0.4266,-8.4844],
    'button_center_z':-7.8,'button_opening_y':6.6,'button_opening_z':4.8,
}

def rounded_box(w,h,t,r,z=0):
    return cq.Workplane('XY',origin=(0,0,z)).rect(w,h).extrude(t).edges('|Z').fillet(r)

def ear(left=True):
    pts=[(-19.5,19.0),(-18.2,33.0),(-5.6,23.5)]
    if not left: pts=[(-x,y) for x,y in pts][::-1]
    # Rounded triangle profile, thick enough to survive handling.
    return (cq.Workplane('XY',origin=(0,0,-P['body_depth']))
            .sketch().polygon(pts).vertices().fillet(1.5).finalize()
            .extrude(P['body_depth']).edges('#Z').fillet(0.65))

def cut_side_opening(shape,x,y,z,w,h):
    # Rounded rectangular aperture through the side wall, millimetres.
    cutter=(cq.Workplane('YZ',origin=(x,y,z)).rect(w,h).extrude(12)
            .edges('|X').fillet(0.8))
    return shape.cut(cutter)

def make_body():
    body=rounded_box(P['body_width'],P['body_height'],P['body_depth'],P['body_radius'],-P['body_depth'])
    body=body.edges('#Z').fillet(0.65).union(ear(True)).union(ear(False))
    cavity=rounded_box(P['glass_width']+2*P['radial_clearance'],
                       P['glass_height']+2*P['radial_clearance'],
                       P['body_depth']-P['glass_recess']+0.2,5.0,-P['body_depth']-0.2)
    window=rounded_box(P['glass_width']-2*P['window_overlap'],
                       P['glass_height']-2*P['window_overlap'],2.0,4.3,-1.0)
    body=body.cut(cavity).cut(window)
    body=cut_side_opening(body,-26,P['usb_center_y'],P['usb_center_z'],P['usb_opening_y'],P['usb_opening_z'])
    for y in P['button_y']:
        body=cut_side_opening(body,14.5,y,P['button_center_z'],P['button_opening_y'],P['button_opening_z'])
    for x,y in P['screw_positions']:
        bore=(cq.Workplane('XY',origin=(x,y,-13.2))
              .circle(P['screw_pilot_diameter']/2).extrude(5.5))
        body=body.cut(bore)
    return body.clean()

def make_lid():
    lid=rounded_box(P['body_width'],P['body_height'],P['lid_thickness'],P['body_radius'],-14.8)
    # Rear outside edge rounded; locating tongue is a clearance fit, not a snap.
    lid=lid.edges('#Z').fillet(0.35)
    lip=rounded_box(33.23,41.23,1.5,4.9,-13.0).cut(rounded_box(30.23,38.23,1.8,3.4,-13.1))
    lid=lid.union(lip)
    for x,y,r in P['board_pad_positions']:
        # Narrow contact tips sit below unpopulated PCB locations, with a gap
        # for 0.5mm nonconductive soft pads. Never put foam on ICs or switches.
        post=cq.Workplane('XY',origin=(x,y,-13.0)).circle(r).extrude(P['pad_top_z']+13.0)
        lid=lid.union(post)
    for x,y in P['screw_positions']:
        bore=cq.Workplane('XY',origin=(x,y,-15)).circle(P['screw_clearance_diameter']/2).extrude(3)
        head=cq.Workplane('XY',origin=(x,y,-14.9)).circle(P['screw_head_diameter']/2).extrude(0.9)
        lid=lid.cut(bore).cut(head)
    # Vent / buzzer exit slots in the removable cap, no metallic decoration.
    for x in (-5,0,5):
        vent=(cq.Workplane('XY',origin=(x,3,-15)).slot2D(10,1.6,90).extrude(2.2))
        lid=lid.cut(vent)
    return lid.clean()

def make_stand():
    # Optional simple upright cradle: case lifts straight out for shaking.
    # Assembly vertical is Y; separate print rotation puts the base on the bed.
    base=cq.Workplane('XY').box(36,3,28).translate((0,-26.7,-7.0)).edges('|Y').fillet(4)
    front=cq.Workplane('XY').box(31,5,2.4).translate((0,-22.7,1.8)).edges('|Y').fillet(1)
    rear=cq.Workplane('XY').box(31,5,2.6).translate((0,-22.7,-16.8)).edges('|Y').fillet(1)
    stand=base.union(front).union(rear).clean()
    return stand

def shape_of(obj): return obj.val() if hasattr(obj,'val') else obj

def mesh_info(path):
    m=trimesh.load_mesh(path,process=True)
    return dict(watertight=bool(m.is_watertight),winding_consistent=bool(m.is_winding_consistent),
                connected_parts=len(m.split()),bounds_mm=m.bounds.round(3).tolist(),
                dimensions_mm=m.extents.round(3).tolist(),volume_mm3=round(float(m.volume),2),triangles=len(m.faces))

def export_model(name,obj,print_obj):
    cq.exporters.export(obj,str(ROOT/'assembly'/f'{name}.stl'),tolerance=0.03,angularTolerance=0.10)
    cq.exporters.export(obj,str(ROOT/'step'/f'{name}.step'))
    cq.exporters.export(print_obj,str(ROOT/'print'/f'{name}.stl'),tolerance=0.03,angularTolerance=0.10)
    return mesh_info(ROOT/'print'/f'{name}.stl')

def main():
    body,lid,stand=make_body(),make_lid(),make_stand()
    reports={}
    # Front face down, cap outside down, stand bottom down; min z=0 for slicing.
    body_print=body.rotate((0,0,0),(1,0,0),180)
    lid_print=lid.translate((0,0,14.8))
    stand_print=stand.rotate((0,0,0),(1,0,0),90)
    b=stand_print.val().BoundingBox()
    stand_print=stand_print.translate((0,0,-b.zmin))
    for name,obj,po in [('body',body,body_print),('lid',lid,lid_print),('stand',stand,stand_print)]:
        info=export_model(name,obj,po)
        info['cad_valid']=bool(obj.val().isValid())
        info['cad_solid_count']=len(obj.solids().vals())
        reports[name]=info
        print(name,json.dumps(info))
        if not info['watertight'] or not info['cad_valid'] or info['connected_parts']!=1:
            raise RuntimeError(name+' is not one valid closed printable part')
    overlaps={}
    for an,a,bn,b in [('body',body,'lid',lid),('body',body,'stand',stand),('lid',lid,'stand',stand)]:
        overlaps[f'{an}_{bn}']=a.val().intersect(b.val()).Volume()
    # Official board keeps its original orientation: USB is negative X.
    board=cq.importers.importStep(str(ROOT/'reference/board-3d/ESP32-S3-Touch-LCD-1_69.stp'))
    board=board.translate((0,-P['glass_center_raw_y'],-P['glass_front_raw_z']-P['glass_recess']))
    # Per-solid intersection avoids boolean union of a complex PCB assembly.
    for name,obj in [('body',body),('lid',lid)]:
        collisions=[]
        part=obj.val()
        for i,solid in enumerate(board.solids().vals()):
            volume=part.intersect(solid).Volume()
            if volume>0.001: collisions.append(dict(board_solid=i,volume_mm3=round(volume,5)))
        overlaps[name+'_board']=collisions
        print('intersections',name,collisions)
    assembly=cq.Assembly(name='Cat_case_V01')
    assembly.add(body,name='cat_body',color=cq.Color(0.12,0.13,0.15))
    assembly.add(lid,name='removable_lid',color=cq.Color(0.14,0.15,0.17))
    assembly.add(stand,name='optional_stand',color=cq.Color(0.19,0.20,0.23))
    assembly.save(str(ROOT/'step/cat_case_assembly.step'))
    (ROOT/'parameters.json').write_text(json.dumps(P,indent=2),encoding='utf-8')
    (ROOT/'verification.json').write_text(json.dumps(dict(parts=reports,intersections=overlaps,
        physical_fit_tested=False,reference='Waveshare official STEP, 2024-06-06'),indent=2),encoding='utf-8')
    print('Model generation complete. Physical fitting not yet performed.')

if __name__=='__main__': main()
