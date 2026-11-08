"""Synthetic first-area metadata/material regressions; no original files."""
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace as Obj
import unittest
from unittest.mock import patch

try:
    import numpy
except ImportError:
    numpy = None
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
if numpy is not None:
    import prepare_area as area
    import prepare_field_geometry as geometry


@unittest.skipUnless(numpy is not None,"Offline exporter requires NumPy")
class AreaPreparationTests(unittest.TestCase):
    def test_initial_route_keeps_both_actors_and_vehicle_part_euler_contract(self):
        self.assertEqual(area.INITIAL_EXTERIOR_ACTORS,(0,1,2,3))
        part=Obj(parent_index=0,scale=(1.,1.,1.),rotation=(0.,0.,0.))
        model=Obj(joints=[Obj(),Obj()],model=Obj(bones=[Obj(),part]),
                  rest=[numpy.eye(4),numpy.eye(4)],tracks=[[{},{}]],ends=[0.])
        self.assertEqual(area.npc_retained_parts(model,0x010B0400),((model,1,1),))
        self.assertEqual(area.npc_retained_parts(model,0x01040400),())
        model.rest[0][0,3]=2
        with self.assertRaisesRegex(ValueError,"identity-ancestor"):
            area.npc_retained_parts(model,0x010B0400)
        model.rest[0][0,3]=0
        part.rotation=(0.,3.14,0.)
        with self.assertRaisesRegex(ValueError,"Euler angles"):
            area.npc_retained_parts(model,0x010B0400)

    def test_retained_non_mesh_part_keeps_world_origin_after_render_bones(self):
        class Model:
            tracks=[[]]
            raw=Obj(root_joint=Obj(address=0x10))
            joints=[Obj(address=0x10),Obj(address=0x20)]
            rest=[numpy.eye(4),numpy.eye(4)]
            def world(self,_clip,frame):
                render,part=numpy.eye(4),numpy.eye(4)
                render[0,3]=frame
                part[0,3]=7+2*frame
                return [render,part],[True,True]
            def deform(self,descriptor,world):return world[descriptor[1]]
        model=Model()
        batch=[0]*28;batch[1]=3
        vertex=(0.,)*8+(255,)*4+(0,0.,0.)
        mesh={"model":model,"descriptors":[("rigid",0)]*3,"bone":0,"index":0,
              "dobj":Obj(mobject=Obj(render_mode=0)),"batch":batch,
              "vertices":[vertex]*3,"textures":[(1,1,b"\xff"*4,0,0)]}
        with tempfile.TemporaryDirectory() as output, \
             patch.object(geometry,"material_state",return_value=(0.,)*20), \
             patch.object(geometry.title,"encode_pack",return_value=b"synthetic") as encode:
            report=geometry.prepare_pack_set([mesh],[(0,[0.,1.],False)],60,[(0.,)*12]*2,
                [],"00"*32,Path(output),"test",expected_count=1,format_version=3,
                retained_parts=((model,1,1),))[0]
        args=encode.call_args.args
        self.assertEqual(args[0],2)
        self.assertEqual([vertex[12] for vertex in args[1]],[0,0,0])
        self.assertEqual(report["retained_parts"],[{"source_part":1,"source_joint":1,
            "source_joint_address":"00000020","sampled_bone":1,"transform":"model-space-world"}])
        sampled=args[6];stride=2*48+96+48
        self.assertEqual(struct.unpack_from("<f",sampled,48+12)[0],7)
        self.assertEqual(struct.unpack_from("<f",sampled,stride+48+12)[0],9)

    def test_wes_event_motion_zero_appends_without_changing_locomotion_mapping(self):
        schedules=geometry.wes_motion_schedules(Obj(ends=[10.,30.,30.,17.,7.]))
        self.assertEqual([clip for clip,_,_ in schedules],[1,2,3,0])
        self.assertEqual([len(frames) for _,frames,_ in schedules],[61,61,35,21])
        self.assertEqual([frames[-1] for _,frames,_ in schedules],[30,30,17,10])
        with self.assertRaisesRegex(ValueError,"Wes motion mapping"):
            geometry.wes_motion_schedules(Obj(ends=[30.,30.,30.,17.]))

    def test_transition_and_a_button_tables_have_different_halfword_layouts(self):
        data=bytearray(200)
        struct.pack_into(">II",data,0,16,1)
        struct.pack_into(">II",data,16+44,80,88)
        struct.pack_into(">4I",data,80,96,1,148,1)
        struct.pack_into(">12fHH",data,96,0,0,0,10,0,0,0,10,0,0,0,1,3,5)
        struct.pack_into(">12fHH",data,148,0,0,0,0,10,0,0,0,10,1,0,0,7,0)
        transition=area.source_collision_tables(data,44)[0]
        interaction=area.source_collision_tables(data,48)[0]
        self.assertEqual((transition['flags'],transition['source_id']),(3,5))
        self.assertEqual((interaction['flags'],interaction['source_id']),(0,7))
        self.assertEqual(interaction['raw_halfwords'],[7,0])
        self.assertEqual(transition['vertices'][3],1)

    def test_timg_variants_select_one_image_at_source_key_boundaries(self):
        keys=[Obj(frame=f,value=v,interpolation=Obj(value="CONSTANT")) for f,v in ((0,0),(3,1),(5,2))]
        texture=Obj(texture_id=0,rotation=(0,0,0),scale=(1,1,1),translation=(0,0,0),
                    repeat_s=1,repeat_t=1,blending=1,tev=None)
        dobj=Obj(address=80,mobject=Obj(material=Obj(alpha=1)))
        model=Obj(materials=[{80:({}, {0:{1:keys}})}])
        for frame,selected in ((0,0),(2.999,0),(3,1),(4.999,1),(5,2),(10,2)):
            states=[]
            for variant in range(3):
                mesh={"model":model,"dobj":dobj,"raw_textures":[texture],"bone":0,"image_variant":variant}
                states.append(geometry.material_state(mesh,0,frame,[True])[5])
                self.assertEqual(geometry.material_state(mesh,0,frame,[False])[5],0)
            self.assertEqual(states,[float(i==selected) for i in range(3)])

    def test_camera_pack_retains_independent_sample_clock_and_loop(self):
        camera=Obj(animations=[Obj(end_frame=1,loop=False)])
        with patch.object(area.title,"camera_values",side_effect=lambda _c,f:(f,0,2,0,0,0,0,60,.1,100,1.3,0)):
            packed=area.sampled_camera(camera,"12"*32)
        self.assertEqual(packed[:4],b"C3C1")
        self.assertEqual(struct.unpack_from("<5I",packed,4),(1,64+3*44,64,3,0x42700000))
        self.assertEqual(struct.unpack_from("<II",packed,24),(0,0))
        self.assertEqual(packed[32:64],b"\x12"*32)
        self.assertEqual([struct.unpack_from("<f",packed,64+i*44)[0] for i in range(3)],[0,.5,1])


if __name__=="__main__":unittest.main()
