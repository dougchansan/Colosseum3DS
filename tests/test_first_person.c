#include "colosseum3ds/first_person.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
#define NEAR(a,b) (fabsf((a)-(b))<1e-4f)

int main(void)
{
    C3FirstPerson view; memset(&view, 0, sizeof view);
    C3FieldMap map; memset(&map, 0, sizeof map);
    C3FieldPlayer player; memset(&player, 0, sizeof player);
    C3TitleCamera camera;
    map.scale = 0.5f; map.camera.fov_y_degrees = 40; map.camera.near_plane = 4; map.camera.far_plane = 900;
    map.camera.aspect = 1.6667f; map.camera.roll = 0.3f;
    player.position[0] = 10; player.position[1] = 2; player.position[2] = -5; player.yaw = 1.0f;

    /* Chord: ZL/ZR (Z) or R with X, whichever half arrives last. X alone opens the menu as before. */
    CHECK(!c3_first_person_chord(0x0400, 0x0400));
    CHECK(!c3_first_person_chord(0x0010, 0x0010));
    CHECK(c3_first_person_chord(0x0410, 0x0400) && c3_first_person_chord(0x0410, 0x0010));
    CHECK(c3_first_person_chord(0x0420, 0x0400) && !c3_first_person_chord(0x0420, 0));
    CHECK(!c3_first_person_chord(0x0440, 0x0400)); /* L is not a chord key */

    /* Disabled: input does nothing. */
    c3_first_person_look(&view, 127, 127, 0.1f); CHECK(view.yaw == 0 && view.pitch == 0);

    /* Entering faces the player's direction, level. */
    view.pitch = 0.7f; c3_first_person_toggle(&view, &player, 36);
    CHECK(view.enabled && NEAR(view.yaw, 1.0f) && view.pitch == 0 && view.floor == 36);

    /* Circle pad up walks along the look: c3_field_move's up is camera_yaw + pi. */
    CHECK(NEAR(fmodf(c3_first_person_move_yaw(&view) + 3.14159265f, 6.2831853f), 1.0f));

    /* Camera: eye 14 up and 2 forward (original units x scale), looking along yaw; projection kept. */
    c3_first_person_camera(&view, &map, &player, &camera);
    CHECK(NEAR(camera.eye[0], 10 + sinf(1.0f)) && NEAR(camera.eye[1], 2 + 7) && NEAR(camera.eye[2], -5 + cosf(1.0f)));
    float d[3] = {camera.target[0]-camera.eye[0], camera.target[1]-camera.eye[1], camera.target[2]-camera.eye[2]};
    float len = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    CHECK(NEAR(d[0]/len, sinf(1.0f)) && NEAR(d[1], 0) && NEAR(d[2]/len, cosf(1.0f)));
    CHECK(camera.fov_y_degrees == 40 && camera.far_plane == 900 && camera.aspect == 1.6667f && camera.roll == 0);
    CHECK(NEAR(camera.near_plane, 0.5f)); /* capped at one original unit */
    map.camera.near_plane = 0.25f; c3_first_person_camera(&view, &map, &player, &camera); CHECK(camera.near_plane == 0.25f);

    /* Stick: deadzone, 2 rad/s at full deflection, right turns to decreasing yaw, up looks up. */
    c3_first_person_look(&view, 14, -14, 1.0f); CHECK(NEAR(view.yaw, 1.0f) && view.pitch == 0);
    c3_first_person_look(&view, 127, 0, 0.1f); CHECK(NEAR(view.yaw, 0.8f));
    c3_first_person_look(&view, 0, 127, 0.1f); CHECK(NEAR(view.pitch, 0.2f));
    c3_first_person_camera(&view, &map, &player, &camera); CHECK(camera.target[1] > camera.eye[1]);
    /* A stalled frame is bounded; pitch never reaches vertical. */
    for (int i = 0; i < 40; ++i) c3_first_person_look(&view, 0, 127, 5.0f);
    CHECK(NEAR(view.pitch, C3_FIRST_PERSON_PITCH_LIMIT));
    for (int i = 0; i < 80; ++i) c3_first_person_look(&view, 0, -127, 0.25f);
    CHECK(NEAR(view.pitch, -C3_FIRST_PERSON_PITCH_LIMIT));
    /* Yaw wraps into [0, 2pi). */
    view.pitch = 0; view.yaw = 0.05f; c3_first_person_look(&view, 127, 0, 0.1f); CHECK(view.yaw >= 0 && view.yaw < 6.2832f && NEAR(view.yaw, 6.2831853f - 0.15f));

    /* Touch drag: first contact only anchors; dragging right turns right, up looks up; release re-anchors. */
    view.yaw = 1.0f; view.pitch = 0;
    c3_first_person_touch(&view, 1, 100, 100); CHECK(NEAR(view.yaw, 1.0f) && view.touch_active);
    c3_first_person_touch(&view, 1, 110, 90); CHECK(NEAR(view.yaw, 1.0f - 0.12f) && NEAR(view.pitch, 0.12f));
    c3_first_person_touch(&view, 0, 0, 0); CHECK(!view.touch_active);
    c3_first_person_touch(&view, 1, 300, 10); CHECK(NEAR(view.yaw, 0.88f));

    /* Same floor keeps the look; a new floor restarts level facing the spawn direction. */
    view.pitch = 0.5f; c3_first_person_arrive(&view, &player, 36); CHECK(NEAR(view.yaw, 0.88f) && view.pitch == 0.5f);
    player.yaw = 4.0f; c3_first_person_arrive(&view, &player, 42); CHECK(NEAR(view.yaw, 4.0f) && view.pitch == 0 && view.floor == 42);

    /* Leaving keeps nothing active; the field camera takes over again. */
    c3_first_person_toggle(&view, &player, 42); CHECK(!view.enabled && !view.touch_active);
    c3_first_person_touch(&view, 1, 5, 5); CHECK(!view.touch_active);
    c3_first_person_arrive(&view, &player, 7); CHECK(view.floor == 42);
    /* Null and non-finite inputs are harmless. */
    c3_first_person_toggle(NULL, &player, 0); c3_first_person_look(NULL, 1, 1, 1); c3_first_person_camera(NULL, &map, &player, &camera);
    c3_first_person_toggle(&view, NULL, 3); CHECK(view.enabled && view.yaw == 0);
    c3_first_person_look(&view, 127, 127, NAN); CHECK(view.yaw == 0 && view.pitch == 0);
    printf("first-person: %u checks passed\n", checks);
    return 0;
}
