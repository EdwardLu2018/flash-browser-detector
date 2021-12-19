#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>

#include <emscripten/emscripten.h>

#include "apriltag.h"
#include "apriltag_pose.h"
#include "tag36h11.h"

#include "common/getopt.h"
#include "common/image_u8.h"
#include "common/image_u8x4.h"
#include "common/pjpeg.h"
#include "common/zarray.h"

#include "lightanchor.h"
#include "lightanchor_detector.h"

// defaults set for 2020 ipad, with 1280x720 images
static apriltag_detection_info_t pose_info = {.cx=636.9118, .cy=360.5100, .fx=997.2827, .fy=997.2827};

static apriltag_family_t *lf = NULL;
static apriltag_detector_t *td = NULL;
static lightanchor_detector_t *ld = NULL;

EMSCRIPTEN_KEEPALIVE
int init()
{
    lf = lightanchor_family_create();
    if (lf == NULL)
        return -1;

    td = apriltag_detector_create();
    if (td == NULL)
        return -1;

    apriltag_detector_add_family(td, lf);

    ld = lightanchor_detector_create();
    if (ld == NULL)
        return -1;

    td->nthreads = 1;
    td->quad_decimate = 1.0;

    td->qtp.max_nmaxima = 5;
    td->qtp.min_cluster_pixels = 0;

    td->qtp.max_line_fit_mse = 10.0;
    td->qtp.cos_critical_rad = cos(10 * M_PI / 180);
    td->qtp.deglitch = 0;

    td->refine_edges = 1;
    td->decode_sharpening = 0.25;

    td->debug = 0;

    ld->ttl_frames = 8;

    ld->thres_dist_shape = 50.0;
    ld->thres_dist_shape_ttl = 20.0;
    ld->thres_dist_center = 25.0;

    return 0;
}

EMSCRIPTEN_KEEPALIVE
int add_code(char code)
{
    return lightanchor_detector_add_code(ld, code);
}

EMSCRIPTEN_KEEPALIVE
int set_detector_options(int range_thres, int min_white_black_diff, int ttl_frames,
                        double thres_dist_shape, double thres_dist_shape_ttl, double thres_dist_center)
{
    ld->range_thres = range_thres;
    td->qtp.min_white_black_diff = min_white_black_diff;
    ld->ttl_frames = ttl_frames;
    ld->thres_dist_shape = thres_dist_shape;
    ld->thres_dist_shape_ttl = thres_dist_shape_ttl;
    ld->thres_dist_center = thres_dist_center;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int set_quad_decimate(float quad_decimate)
{
    td->quad_decimate = quad_decimate;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int save_grayscale(uint8_t pixels[], uint8_t gray[], int cols, int rows)
{
    const int len = cols * rows * 4;
    for (int i = 0, j = 0; i < len; i+=4, j++)
    {
        gray[j] = pixels[i];
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int detect_tags(uint8_t gray[], int cols, int rows)
{
    image_u8_t im = {
        .width = cols,
        .height = rows,
        .stride = cols,
        .buf = gray
    };

    zarray_t *detections = apriltag_detector_detect(td, &im);

    int sz = zarray_size(detections);

    for (int i = 0; i < sz; i++) {
        apriltag_detection_t *det;
        zarray_get(detections, i, &det);

        EM_ASM({
            const tag = {};

            tag["code"] = $0;

            tag["corners"] = [];

            const corner0 = {};
            corner0["x"] = $1;
            corner0["y"] = $2;
            tag["corners"].push(corner0);

            const corner1 = {};
            corner1["x"] = $3;
            corner1["y"] = $4;
            tag["corners"].push(corner1);

            const corner2 = {};
            corner2["x"] = $5;
            corner2["y"] = $6;
            tag["corners"].push(corner2);

            const corner3 = {};
            corner3["x"] = $7;
            corner3["y"] = $8;
            tag["corners"].push(corner3);

            const center = {};
            center["x"] = $9;
            center["y"] = $10;
            tag["center"] = center;

            const tagEvent = new CustomEvent("onFlashTagFound", {detail: {tag: tag}});
            var scope;
            if ('function' === typeof importScripts)
                scope = self;
            else
                scope = window;
            scope.dispatchEvent(tagEvent);
        },
            det->id,
            det->p[0][0],
            det->p[0][1],
            det->p[1][0],
            det->p[1][1],
            det->p[2][0],
            det->p[2][1],
            det->p[3][0],
            det->p[3][1],
            det->c[0],
            det->c[1]
        );

        EM_ASM_({
            var $a = arguments;
            var i = 0;

            const H = [];
            H[0] = $a[i++];
            H[1] = $a[i++];
            H[2] = $a[i++];
            H[3] = $a[i++];
            H[4] = $a[i++];
            H[5] = $a[i++];
            H[6] = $a[i++];
            H[7] = $a[i++];
            H[8] = $a[i++];

            const tagEvent = new CustomEvent("onFlashHomoFound", {detail: {H: H}});
            var scope;
            if ('function' === typeof importScripts)
                scope = self;
            else
                scope = window;
            scope.dispatchEvent(tagEvent);
        },
            MATD_EL(det->H,0,0),
            MATD_EL(det->H,0,1),
            MATD_EL(det->H,0,2),
            MATD_EL(det->H,1,0),
            MATD_EL(det->H,1,1),
            MATD_EL(det->H,1,2),
            MATD_EL(det->H,2,0),
            MATD_EL(det->H,2,1),
            MATD_EL(det->H,2,2)
        );

        pose_info.det = det;
        pose_info.tagsize = 0.15;

        double err1, err2;
        apriltag_pose_t pose1, pose2;
        // EM_ASM({console.time("pose estimation")});
        estimate_tag_pose_orthogonal_iteration(&pose_info, &err1, &pose1, &err2, &pose2, 50);
        // EM_ASM({console.time("pose estimation")});

        EM_ASM_({
            var $a = arguments;
            var i = 0;

            const rot = [];
            rot[0] = $a[i++];
            rot[1] = $a[i++];
            rot[2] = $a[i++];
            rot[3] = $a[i++];
            rot[4] = $a[i++];
            rot[5] = $a[i++];
            rot[6] = $a[i++];
            rot[7] = $a[i++];
            rot[8] = $a[i++];

            const trans = [];
            trans[0] = $a[i++];
            trans[1] = $a[i++];
            trans[2] = $a[i++];

            const tagEvent = new CustomEvent("onFlashPoseFound", {detail: {pose: {R: rot, T: trans}}});
            var scope;
            if ('function' === typeof importScripts)
                scope = self;
            else
                scope = window;
            scope.dispatchEvent(tagEvent);
        },
            MATD_EL(pose1.R,0,0),
            MATD_EL(pose1.R,0,1),
            MATD_EL(pose1.R,0,2),
            MATD_EL(pose1.R,1,0),
            MATD_EL(pose1.R,1,1),
            MATD_EL(pose1.R,1,2),
            MATD_EL(pose1.R,2,0),
            MATD_EL(pose1.R,2,1),
            MATD_EL(pose1.R,2,2),
            MATD_EL(pose1.t,0,0),
            MATD_EL(pose1.t,0,1),
            MATD_EL(pose1.t,0,2)
        );
    }

    apriltag_detections_destroy(detections);

    return sz;
}
