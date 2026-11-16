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

    def test_person_rows_keep_signed_facing_flags_and_people_info(self):
        def rel(module,sections,section,offset,size):
            data=bytearray(offset+size)
            struct.pack_into(">I",data,0,module);struct.pack_into(">I",data,12,sections)
            struct.pack_into(">I",data,16,0x40)
            struct.pack_into(">II",data,0x40+section*8,offset,size)
            return data
        common=rel(125,11,4,0x100,0x14A800)
        struct.pack_into(">I",common,0x100+0x14A6E8,150)
        info=0x100+0x13E440+0x19*44
        common[info]=0x04;common[info+1:info+10]=bytes((1,2,6,255,255,255,255,255,13))
        struct.pack_into(">I",common,info+12,0x00F60400)
        struct.pack_into(">3f",common,info+16,9,13,3)
        local=rel(15,10,3,0x100,0xA0+2*36)
        struct.pack_into(">6I3f",local,0x100+0xA0,0xF2900000,0xFFE20019,0,1,0,0x01000008,30,0,300)
        struct.pack_into(">6I3f",local,0x100+0xA0+36,0x10000000,0x00B40019,0x025C0000,1,0x0100000A,0,-50,33,-10)
        first,second=area.actor_records(bytes(local),15,bytes(common),0xA0,2,"town")
        # floor_character.c reads the facing as s16 degrees: 0xFFE2 is -30.
        self.assertEqual((first["yaw_degrees"],second["yaw_degrees"]),(-30,180))
        self.assertEqual((first["people_info"],first["model_resource"]),(0x19,0x00F60400))
        self.assertEqual((first["visible_initial"],first["load_initial"],first["move_type"]),(True,True,2))
        self.assertEqual((second["visible_initial"],second["load_initial"]),(False,False))
        self.assertEqual((first["talk_start_type"],first["talk_end_type"]),(2,1))
        self.assertEqual((first["talk_script"],second["move_script"]),(0x01000008,0x0100000A))
        self.assertEqual(first["motion_indices"],[1,2,6,255,255,255,255,255,13])
        self.assertEqual([round(v,5) for v in first["position"]],[3.0,0.0,30.0])
        self.assertEqual([round(v,5) for v in second["position"]],[-5.0,3.3,-1.0])
        self.assertEqual([round(first[key],5) for key in ("walk_distance","run_distance","radius")],[.9,1.3,.3])
        self.assertEqual((first["file"],second["file"]),("town_npc0.c3t","town_npc1.c3t"))
        self.assertEqual(first["raw_words"][:2],[0xF2900000,0xFFE20019])
        struct.pack_into(">I",local,0x100+0xA0+4,0x00000096)  # PeopleInfo index 150
        with self.assertRaisesRegex(ValueError,"PeopleInfo"):
            area.actor_records(bytes(local),15,bytes(common),0xA0,2,"town")
        with self.assertRaises(ValueError):
            area.actor_records(bytes(local),15,bytes(common),0xA0,3,"town")  # past the data section

    def test_records_sharing_a_model_resource_reuse_the_first_pack(self):
        def record(index,resource,motions=(1,2,3)):
            return {"index":index,"model_resource":resource,"file":f"town_npc{index}.c3t",
                    "motion_indices":[*motions,255,255,255,255,255,9],"default_idle_clip":motions[0],
                    "load_initial":index!=0,"visible_initial":index<2}
        actors=[record(0,0xA00400),record(1,0xB00400),record(2,0xA00400,(4,2,3)),record(3,0xA00400)]
        groups=area.share_models(actors)
        self.assertEqual([(owner["index"],[a["index"] for a in sharers]) for owner,sharers in groups],
                         [(0,[2,3]),(1,[])])
        for owner,sharers in groups:  # the fields npc_pack records on the owner
            owner.update(source_clip_ends=[10.,20.,30.,40.,50.],particle_callbacks_unrendered=[[]],
                         source_part_bones={"1":9} if owner["index"]==0 else {})
            for actor in sharers:
                area.adopt_shared_pack(actor,owner)
        self.assertEqual([a["file"] for a in actors],
                         ["town_npc0.c3t","town_npc1.c3t","town_npc0.c3t","town_npc0.c3t"])
        self.assertEqual([a.get("shares_model_with") for a in actors],[None,None,0,0])
        self.assertEqual(actors[2]["default_idle_clip"],4)  # its own PeopleInfo idle
        self.assertEqual(actors[2]["source_part_bones"],{"1":9})
        actors[2]["source_part_bones"]["1"]=3  # copies never alias the owner
        self.assertEqual(actors[0]["source_part_bones"],{"1":9})
        with self.assertRaisesRegex(ValueError,"same original model resource"):
            area.adopt_shared_pack(actors[1],actors[0])
        with self.assertRaisesRegex(ValueError,"same original model resource"):
            area.adopt_shared_pack(actors[0],actors[0])
        reports=[{"file":"town_npc0.c3t","bytes":1000,"vertices":300,"textures":2,"sampled_bones":17,
                  "clips":[{"samples":21},{"samples":61}]},
                 {"file":"town_npc1.c3t","bytes":4000,"vertices":600,"textures":3,"sampled_bones":63,
                  "clips":[{"samples":5}]}]
        actors[1]["source_clip_ends"]=[10.]  # idle clip 1 is not in a one-clip pack
        bounds=area.actor_bounds(actors,reports)
        self.assertEqual(bounds,{"actors":4,"actors_with_model":4,"actors_without_model":[],
            "model_packs":2,"model_pack_bytes":5000,"model_pack_vertices":900,"model_pack_textures":5,
            "largest_pack_bytes":4000,"max_sampled_bones":63,"max_clips":2,"max_samples":82,
            "initially_loaded":[1,2,3],"initially_visible":[0,1],"idle_motion_outside_pack":[1]})
        actors[3]["file"]="town_npc9.c3t"
        with self.assertRaisesRegex(ValueError,"not prepared"):
            area.actor_bounds(actors,reports)

    def test_partner_pack_keeps_wes_locomotion_slots_then_listed_motions(self):
        ends=[10.,30.,30.,17.,30.,5.,26.,30.]+[8.]*40
        ends[14]=15.;ends[44]=10.
        schedules=area.partner_schedules(ends)
        self.assertEqual([clip for clip,_,_ in schedules],[1,2,3,0,4,7,14,21,22,23,27,28,29,42,43,44])
        self.assertEqual([loop for _,_,loop in schedules],[True]*3+[False]*13)
        self.assertEqual([len(frames) for _,frames,_ in schedules][:7],[61,61,35,21,61,61,31])
        self.assertEqual(schedules[6][1][-1],15.)
        self.assertLessEqual(sum(len(frames) for _,frames,_ in schedules),4096)
        with self.assertRaisesRegex(ValueError,"lacks a listed source motion"):
            area.partner_schedules(ends[:44])
        still=area.partner_schedules([0.]*45,{"clips":(1,2,3,0),"locomotion":(1,2,3)})
        self.assertEqual([frames for _,frames,_ in still],[[0.]]*4)

    def test_profile_mode_covers_later_floors_only(self):
        floors=sorted(area.profile_floors())
        self.assertEqual(len(floors),12)
        self.assertTrue(all(name.startswith("phenac") for name in floors))
        self.assertNotIn("outskirt",floors);self.assertNotIn("shop",floors)
        self.assertEqual(area.PROFILES["S1_shop_1F.fsys"][0],2947200)
        self.assertEqual(area.INITIAL_EXTERIOR_ACTORS,(0,1,2,3))

    def test_profile_archive_admission_is_exact(self):
        import hashlib
        with tempfile.TemporaryDirectory() as temp:
            files=Path(temp)/"files";files.mkdir()
            data=b"FSYS"+bytes(60)
            profile={"archive":"X1_out.fsys","bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}
            with self.assertRaisesRegex(ValueError,"path or size"):
                area.admit_profile_archive(temp,profile)
            (files/"X1_out.fsys").write_bytes(data)
            self.assertEqual(area.admit_profile_archive(temp,profile),data)
            (files/"X1_out.fsys").write_bytes(data+b"\0")
            with self.assertRaisesRegex(ValueError,"path or size"):
                area.admit_profile_archive(temp,profile)
            (files/"X1_out.fsys").write_bytes(b"GSYS"+bytes(60))
            with self.assertRaisesRegex(ValueError,"differs"):
                area.admit_profile_archive(temp,profile)

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
                    repeat_s=1,repeat_t=1,wrap_s=0,wrap_t=0,blending=1,tev=None)
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
