"""Host regressions for preserving live field bindings across scene ownership."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'platform/3ds/field_scene.c'


class FieldBattleHandoffTests(unittest.TestCase):
    def test_handoff_drains_before_release_and_restores_without_story_entry(self):
        source = SOURCE.read_text()
        handoff = source[source.index('c3_n3ds_log_memory(trace,"field_battle_before_drain")'):
                         source.index('/* Modal preview time')]
        ordered = ['C3_RECOVER_DRAIN', 'release_field_models(storage,count)',
                   'c3_battle_preview_from_game_state', '"battle-to-field"',
                   'restore_field_models(', 'c3_stage_renderer_init(',
                   'c3_stage_renderer_set_tv(']
        positions = [handoff.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        for forbidden in ('c3_field_story_load(', 'c3_field_story_prepare(',
                          'c3_field_story_enter(', 'c3_field_story_tick(',
                          'c3_game_state_load('):
            self.assertNotIn(forbidden, handoff)
        self.assertIn('preview_result>0?C3_RECOVER_EXIT:C3_RECOVER_DRAIN', handoff)

    def test_payload_reload_keeps_story_model_addresses_and_cleans_partial_failure(self):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(compiler[0]):
            self.skipTest('A host C compiler is required')
        source = SOURCE.read_text()
        helpers = source[source.index('static void release_field_models('):
                         source.index('static int save_game')]
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define C3_STORY_ERROR 128
typedef struct { int payload, sampled; } C3Title;
typedef struct { C3Title *hero, *npc; int ticks, pending; } C3FieldStory;
static int loads, fail_load, fail_sample;
static void c3_title_close(C3Title *m) { memset(m,0,sizeof *m); }
static int c3_title_load(C3Title *m,const char *path,char *e,size_t n) {
    (void)e;(void)n;assert(path[0]);
    if(++loads==fail_load)return 0;
    m->payload=1;return 1;
}
static int c3_title_animate(C3Title *m,uint32_t clip,float time) {
    assert(m->payload && clip==0 && time==12.5f);m->sampled=1;return 1;
}
static int c3_field_story_sample(C3FieldStory *s,char *e) {
    (void)e;assert(s->hero->payload && s->npc->payload);
    s->hero->sampled=7;s->npc->sampled=9;return !fail_sample;
}
'''
        harness += helpers + r'''
int main(void) {
    C3Title models[3]={{1,0},{1,0},{1,0}};
    C3FieldStory story={models,models+2,42,10};
    char paths[3][160]={"map","wes","npc"};char error[128]={0};
    for(int repeat=0;repeat<4;++repeat){
        release_field_models(models,3);
        assert(!models[0].payload && story.hero==models && story.npc==models+2);
        loads=0;fail_load=0;
        assert(restore_field_models(models,3,paths,&story,12.5f,error,sizeof error));
        assert(story.hero->sampled==7 && story.npc->sampled==9);
        assert(story.ticks==42 && story.pending==10);
    }
    for(int failure=1;failure<=3;++failure){
        release_field_models(models,3);loads=0;fail_load=failure;
        assert(!restore_field_models(models,3,paths,&story,12.5f,error,sizeof error));
        release_field_models(models,3);
        for(int i=0;i<3;++i)assert(!models[i].payload);
    }
    loads=0;fail_load=0;fail_sample=1;
    assert(!restore_field_models(models,3,paths,&story,12.5f,error,sizeof error));
    release_field_models(models,3);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(harness)
            executable = path / 'handoff'
            flags = shlex.split(os.environ.get('CFLAGS', '-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
            result = subprocess.run(compiler + flags + [str(path / 'test.c'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
