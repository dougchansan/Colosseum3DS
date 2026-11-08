"""Exact aligned-scale cancellation and no-surface PDA omission regressions."""
from pathlib import Path
import sys
import types
import unittest
try:
    import numpy as np
except ImportError:
    raise unittest.SkipTest("NumPy is required by the optional geometry exporter")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_field_geometry import Model, srt_matrix
from prepare_pda_models import omit_collapsed_meshes


def model_for(scales, flags):
    model = Model.__new__(Model)
    bones = [types.SimpleNamespace(parent_index=i-1 if i else None,
        position=(.1*i,.2*i,-.1*i), rotation=(.23*i,-.17*i,.31*i),
        scale=np.asarray(scale), flags=flag, is_hidden=False)
        for i,(scale,flag) in enumerate(zip(scales,flags))]
    model.model = types.SimpleNamespace(bones=bones)
    model.tracks=[]
    model.upper={};model.lower={};model.effector={};model.copy_position={};model._cache={}
    return model


class AlignedScaleTests(unittest.TestCase):
    def test_division_free_world_matches_original_nonsingular_composition(self):
        scales=[(2.,3.,4.),(-.5,2.,1.),(3.,.3,-2.),(.7,1.2,2.)]
        for flags in ([0,0,0,0],[8,8,8,0],[0,8,0,8]):
            model=model_for(scales,flags)
            expected=[];accumulated=[]
            for bone in model.model.bones:
                parent=bone.parent_index
                pa=accumulated[parent] if parent is not None else None
                accumulated.append((pa if bone.flags&8 else bone.scale*pa) if pa is not None else bone.scale)
                local=srt_matrix(bone.scale,bone.rotation,bone.position,pa)
                expected.append(expected[parent]@local if parent is not None else local)
            np.testing.assert_allclose(model.world(0,0)[0],expected,atol=1e-14)

    def test_authored_zero_parent_scale_stays_exactly_collapsed(self):
        model=model_for([(0.,0.,0.),(3.,4.,5.),(6.,7.,8.)],[0,0,0])
        world,_=model.world(0,0)
        self.assertTrue(np.isfinite(world).all())
        self.assertTrue(np.all(world[:,:3,:3]==0))
        np.testing.assert_array_equal(world[:,:3,3],np.zeros((3,3)))

    def test_partial_zero_scale_uses_finite_cancelled_limit(self):
        exact=model_for([(0.,2.,3.),(3.,4.,5.),(6.,7.,8.)],[0,0,0]).world(0,0)[0]
        nearby=model_for([(1e-8,2.,3.),(3.,4.,5.),(6.,7.,8.)],[0,0,0]).world(0,0)[0]
        np.testing.assert_allclose(exact,nearby,atol=1e-6)

    def test_omission_requires_all_samples_collapsed_and_no_surface_triangle(self):
        model=Model.__new__(Model)
        desc=[('rigid',i) for i in range(3)]
        collapsed=np.array([np.eye(4) for _ in range(3)])
        collapsed[:,:3,:3]=0
        collapsed[1,0,3]=1;collapsed[2,1,3]=1
        line={'model':model,'descriptors':[desc[0],desc[1],desc[0]]}
        triangle={'model':model,'descriptors':desc}
        retained,omitted=omit_collapsed_meshes([line,triangle],[(collapsed,None)])
        self.assertEqual(omitted,[0]);self.assertEqual(retained,[triangle])
        visible=collapsed.copy();visible[0,:3,:3]=np.eye(3)
        retained,omitted=omit_collapsed_meshes([line],[(collapsed,None),(visible,None)])
        self.assertEqual(omitted,[]);self.assertEqual(retained,[line])

    def test_animated_collapsed_accessory_splits_without_hiding_body(self):
        from prepare_field_geometry import _sample_descriptor
        model=Model.__new__(Model)
        model.rest=np.array([np.eye(4),np.eye(4)])
        visible=model.rest.copy()
        collapsed=visible.copy();collapsed[1,:3,:3]=0
        states=[(collapsed,None),(visible,None)]
        body=('rigid',0);accessory=('skin',0,((1,1.),))
        mesh={'model':model,'descriptors':[body]*3+[accessory]*3,
              'vertices':[(0.,0.,0.)]*6,'batch':[0,6]}
        retained,omitted=omit_collapsed_meshes([mesh],states,2,[0.,.5])
        self.assertEqual(omitted,[])
        self.assertEqual(len(retained),2)
        self.assertEqual(retained[0]['collapsed_samples'],set())
        self.assertEqual(retained[1]['collapsed_samples'],{(2,0.)})
        self.assertEqual([m['batch'][1] for m in retained],[3,3])
        track=_sample_descriptor(model,accessory,states)
        self.assertTrue(np.isfinite(track).all())
        np.testing.assert_array_equal(track[0],collapsed[1,:3])
        np.testing.assert_array_equal(track[1],visible[1,:3])
        # Without a proof that every affected triangle is hidden, fail closed.
        model._collapsed_world_descriptors={}
        with self.assertRaisesRegex(ValueError,'Singular sampled'):
            _sample_descriptor(model,accessory,states)

if __name__=='__main__':unittest.main()
