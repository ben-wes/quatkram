/*
faccwrap~ - Float accumulator and wrapper external for Pure Data

2024, Ben Wesch

Functionality:
- Accumulates incoming float values on a sample-by-sample basis
- Wraps the accumulated value between -1 and 1
- Maintains an internal float state that represents the accumulated value

Usage:
1. Send a signal to the inlet (float values to accumulate)
2. Receive the resulting signal from the outlet (accumulated and wrapped value)
3. Use 'set <value>' message to reset the internal state to a specific value

Note: This code was developed with assistance from the Anthropic Claude AI language model.
*/

#include "m_pd.h"
#include <math.h>

static t_class *faccwrap_tilde_class;

typedef struct _faccwrap_tilde {
    t_object  x_obj;
    t_sample f_dummy;  // dummy float for signal inlet
    t_float f_accum;   // Accumulated float value
    t_outlet *x_out;   // Signal outlet
} t_faccwrap_tilde;

t_int *faccwrap_tilde_perform(t_int *w)
{
    t_faccwrap_tilde *x = (t_faccwrap_tilde *)(w[1]);
    t_sample *in = (t_sample *)(w[2]);
    t_sample *out = (t_sample *)(w[3]);
    int n = (int)(w[4]);

    t_float accum = x->f_accum;

    while (n--) {
        // Accumulate the input
        accum += *in++;

        // Wrap between -1 and 1
        accum = remainder(accum, 2);

        // Output the wrapped result
        *out++ = accum;
    }

    // Store the final accumulated state for the next DSP cycle
    x->f_accum = accum;

    return (w+5);
}

void faccwrap_tilde_dsp(t_faccwrap_tilde *x, t_signal **sp)
{
    dsp_add(faccwrap_tilde_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}

void faccwrap_tilde_set(t_faccwrap_tilde *x, t_floatarg f)
{
    x->f_accum = remainder(f, 2);
}

void *faccwrap_tilde_new(void)
{
    t_faccwrap_tilde *x = (t_faccwrap_tilde *)pd_new(faccwrap_tilde_class);

    // Initialize accumulated value to 0
    x->f_accum = 0;

    x->x_out = outlet_new(&x->x_obj, &s_signal);

    return (void *)x;
}

void faccwrap_tilde_setup(void)
{
    faccwrap_tilde_class = class_new(gensym("faccwrap~"),
        (t_newmethod)faccwrap_tilde_new,
        0, sizeof(t_faccwrap_tilde),
        CLASS_DEFAULT,
        0);

    class_addmethod(faccwrap_tilde_class, (t_method)faccwrap_tilde_dsp, gensym("dsp"), A_CANT, 0);
    class_addmethod(faccwrap_tilde_class, (t_method)faccwrap_tilde_set, gensym("set"), A_FLOAT, 0);
    CLASS_MAINSIGNALIN(faccwrap_tilde_class, t_faccwrap_tilde, f_dummy);
}