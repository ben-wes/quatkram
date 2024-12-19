#include "m_pd.h"
#include <string.h>
#include <math.h>

static t_class *tetra2pos_class;

typedef struct _tetra2pos {
    t_object x_obj;
    t_float edge_length;  // in mm
    t_float positions[4][3]; // actual positions in mm
    int debug;
    t_outlet *position_out;  // position in mm
} t_tetra2pos;

static void calculate_positions(t_tetra2pos *x) {
    t_float a = x->edge_length;  // edge length
    t_float sqrt6 = sqrt(6.0f);
    t_float h = a * sqrt(3.0f/8.0f);  // height
    
    // Front mic
    x->positions[0][0] = 0;
    x->positions[0][1] = a/sqrt6;
    x->positions[0][2] = 0;
    
    // Left back mic
    x->positions[1][0] = -a/2;
    x->positions[1][1] = -a/(2*sqrt6);
    x->positions[1][2] = 0;
    
    // Right back mic
    x->positions[2][0] = a/2;
    x->positions[2][1] = -a/(2*sqrt6);
    x->positions[2][2] = 0;
    
    // Top mic
    x->positions[3][0] = 0;
    x->positions[3][1] = 0;
    x->positions[3][2] = h;
}

static void tetra2pos_debug(t_tetra2pos *x, t_floatarg f) {
    x->debug = (int)f;
}

static void tetra2pos_edge(t_tetra2pos *x, t_floatarg f) {
    if (f <= 0) {
        pd_error(x, "tetra2pos: edge length must be positive");
        return;
    }
    x->edge_length = f;
    calculate_positions(x);
    
    if (x->debug) {
        post("tetra2pos: edge length set to %.1f mm", x->edge_length);
        post("tetra2pos: mic positions (mm):");
        for (int i = 0; i < 4; i++) {
            post("  %d: %.1f %.1f %.1f", i, x->positions[i][0], x->positions[i][1], x->positions[i][2]);
        }
    }
}

static void solve_linear_system(t_float A[3][3], t_float b[3], t_float result[3]) {
    t_float det = A[0][0]*(A[1][1]*A[2][2] - A[1][2]*A[2][1])
                - A[0][1]*(A[1][0]*A[2][2] - A[1][2]*A[2][0])
                + A[0][2]*(A[1][0]*A[2][1] - A[1][1]*A[2][0]);
                
    if (fabs(det) < 0.0001f) {
        result[0] = result[1] = result[2] = 0;
        return;
    }
    
    for (int i = 0; i < 3; i++) {
        t_float temp[3][3];
        memcpy(temp, A, sizeof(temp));
        for (int j = 0; j < 3; j++) {
            temp[j][i] = b[j];
        }
        t_float det_i = temp[0][0]*(temp[1][1]*temp[2][2] - temp[1][2]*temp[2][1])
                      - temp[0][1]*(temp[1][0]*temp[2][2] - temp[1][2]*temp[2][0])
                      + temp[0][2]*(temp[1][0]*temp[2][1] - temp[1][1]*temp[2][0]);
        result[i] = det_i/det;
    }
}

static void solve_position_toa(t_float distances[4], t_float positions[4][3], t_float result[3]) {
    t_float A[3][3] = {{0}};
    t_float b[3] = {0};
    
    for (int i = 0; i < 3; i++) {
        t_float dx = positions[i+1][0] - positions[0][0];
        t_float dy = positions[i+1][1] - positions[0][1];
        t_float dz = positions[i+1][2] - positions[0][2];
        
        t_float d0_sq = distances[0] * distances[0];
        t_float di_sq = distances[i+1] * distances[i+1];
        
        t_float p0_sq = positions[0][0]*positions[0][0] + 
                       positions[0][1]*positions[0][1] + 
                       positions[0][2]*positions[0][2];
        t_float pi_sq = positions[i+1][0]*positions[i+1][0] + 
                       positions[i+1][1]*positions[i+1][1] + 
                       positions[i+1][2]*positions[i+1][2];
        
        A[i][0] = 2 * dx;
        A[i][1] = 2 * dy;
        A[i][2] = 2 * dz;
        b[i] = d0_sq - di_sq - p0_sq + pi_sq;
    }
    
    solve_linear_system(A, b, result);
}

static void solve_position_tdoa(t_float distances[4], t_float positions[4][3], t_float result[3]) {
    // Step 1: Get the measured differences (these are correct)
    t_float tdoa[3];
    for (int i = 0; i < 3; i++) {
        tdoa[i] = distances[i + 1] - distances[0];
    }
    
    // Step 2: Find smallest possible r1 (distance to first mic) that makes
    // the geometry possible with our measured differences.
    // We know: r2 = r1 + tdoa[0]
    //         r3 = r1 + tdoa[1]
    //         r4 = r1 + tdoa[2]
    // All these distances must satisfy the tetrahedron geometry
    
    // Start with rough guess - half the edge length
    t_float r1 = 0;
    t_float real_distances[4];
    
    // Try increasing r1 until we find distances that work
    // We'll know they work when TOA gives good result
    for (r1 = 100; r1 < 10000; r1 += 10) {
        real_distances[0] = r1;
        real_distances[1] = r1 + tdoa[0];
        real_distances[2] = r1 + tdoa[1];
        real_distances[3] = r1 + tdoa[2];
        
        // Try these distances with TOA solver
        solve_position_toa(real_distances, positions, result);
        
        // Check if this solution makes sense by verifying distances
        t_float error = 0;
        for (int i = 0; i < 4; i++) {
            t_float dx = result[0] - positions[i][0];
            t_float dy = result[1] - positions[i][1];
            t_float dz = result[2] - positions[i][2];
            t_float calc_dist = sqrt(dx*dx + dy*dy + dz*dz);
            error += fabs(calc_dist - real_distances[i]);
        }
        
        // If error is small enough, this is our solution
        if (error < 1.0) {
            break;
        }
    }
}

static void tetra2pos_relative(t_tetra2pos *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    
    if (argc != 4) {
        pd_error(x, "tetra2pos: relative message expects 4 distances (mm)");
        return;
    }

    t_float distances[4];
    for (int i = 0; i < 4; i++) {
        distances[i] = atom_getfloat(argv + i);
    }

    t_float position[3];
    solve_position_tdoa(distances, x->positions, position);

    t_atom position_list[3];
    SETFLOAT(&position_list[0], position[0]);
    SETFLOAT(&position_list[1], position[1]);
    SETFLOAT(&position_list[2], position[2]);
    outlet_list(x->position_out, &s_list, 3, position_list);

    if (x->debug) {
        post("tetra2pos: relative distances (mm): %.1f %.1f %.1f %.1f", 
             distances[0], distances[1], distances[2], distances[3]);
        post("tetra2pos: time differences (mm): %.1f %.1f %.1f", 
             distances[1] - distances[0], 
             distances[2] - distances[0], 
             distances[3] - distances[0]);
        post("tetra2pos: position (mm): %.1f %.1f %.1f",
             position[0], position[1], position[2]);
    }
}

static void tetra2pos_list(t_tetra2pos *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    
    if (argc != 4) {
        pd_error(x, "tetra2pos: expect 4 absolute distances (mm)");
        return;
    }

    t_float distances[4];
    for (int i = 0; i < 4; i++) {
        distances[i] = atom_getfloat(argv + i);
    }

    t_float position[3];
    solve_position_toa(distances, x->positions, position);

    t_atom position_list[3];
    SETFLOAT(&position_list[0], position[0]);
    SETFLOAT(&position_list[1], position[1]);
    SETFLOAT(&position_list[2], position[2]);
    outlet_list(x->position_out, &s_list, 3, position_list);

    if (x->debug) {
        post("tetra2pos: absolute distances (mm): %.1f %.1f %.1f %.1f", 
             distances[0], distances[1], distances[2], distances[3]);
        post("tetra2pos: position (mm): %.1f %.1f %.1f",
             position[0], position[1], position[2]);
    }
}

static void *tetra2pos_new(t_floatarg edge) {
    t_tetra2pos *x = (t_tetra2pos *)pd_new(tetra2pos_class);
    
    x->position_out = outlet_new(&x->x_obj, &s_list);
    x->edge_length = edge > 0 ? edge : 1000.0f;
    x->debug = 0;
    
    calculate_positions(x);
    return (void *)x;
}

void tetra2pos_setup(void) {
    tetra2pos_class = class_new(gensym("tetra2pos"),
        (t_newmethod)tetra2pos_new,
        0,
        sizeof(t_tetra2pos),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0);
    
    class_addlist(tetra2pos_class, tetra2pos_list);
    class_addmethod(tetra2pos_class, (t_method)tetra2pos_debug, gensym("debug"), A_FLOAT, 0);
    class_addmethod(tetra2pos_class, (t_method)tetra2pos_edge, gensym("edge"), A_FLOAT, 0);
    class_addmethod(tetra2pos_class, (t_method)tetra2pos_relative, gensym("relative"), A_GIMME, 0);
}
