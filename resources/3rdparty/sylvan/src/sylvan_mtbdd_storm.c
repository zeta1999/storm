/**
 * Generate SHA2 structural hashes.
 * Hashes are independent of location.
 * Mainly useful for debugging purposes.
 */
static void
mtbdd_sha2_rec(MTBDD mtbdd, SHA256_CTX *ctx)
{
    if (mtbdd == sylvan_true || mtbdd == sylvan_false) {
        SHA256_Update(ctx, (void*)&mtbdd, sizeof(MTBDD));
        return;
    }
    
    mtbddnode_t node = GETNODE(mtbdd);
    if (mtbddnode_isleaf(node)) {
        uint64_t val = mtbddnode_getvalue(node);
        SHA256_Update(ctx, (void*)&val, sizeof(uint64_t));
    } else if (mtbddnode_getmark(node) == 0) {
        mtbddnode_setmark(node, 1);
        uint32_t level = mtbddnode_getvariable(node);
        if (MTBDD_STRIPMARK(mtbddnode_gethigh(node))) level |= 0x80000000;
        SHA256_Update(ctx, (void*)&level, sizeof(uint32_t));
        mtbdd_sha2_rec(mtbddnode_gethigh(node), ctx);
        mtbdd_sha2_rec(mtbddnode_getlow(node), ctx);
    }
}

void
mtbdd_getsha(MTBDD mtbdd, char *target)
{
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    mtbdd_sha2_rec(mtbdd, &ctx);
    if (mtbdd != sylvan_true && mtbdd != sylvan_false) mtbdd_unmark_rec(mtbdd);
    SHA256_End(&ctx, target);
}

/**
 * Binary operation Times (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Integer or Double.
 * If either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_divide, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    if (a == mtbdd_false || b == mtbdd_false) return mtbdd_false;
    
    // Do not handle Boolean MTBDDs...
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            int64_t va = *(int64_t*)(&val_a);
            int64_t vb = *(int64_t*)(&val_b);

            if (va == 0) return a;
            else if (vb == 0) return b;
            else {
                MTBDD result;
                if (va == 1) result = b;
                else if (vb == 1) result = a;
                else result = mtbdd_int64(va*vb);
                return result;
            }
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            if (vval_a == 0.0) return a;
            else if (vval_b == 0.0) return b;
            else {
                MTBDD result;
                if (vval_a == 0.0 || vval_b == 1.0) result = a;
                result = mtbdd_double(vval_a / vval_b);
                return result;
            }
        }
        else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            // both fraction
            uint64_t nom_a = val_a>>32;
            uint64_t nom_b = val_b>>32;
            uint64_t denom_a = val_a&0xffffffff;
            uint64_t denom_b = val_b&0xffffffff;
            // multiply!
            uint32_t c = gcd(denom_b, denom_a);
            uint32_t d = gcd(nom_a, nom_b);
            nom_a /= d;
            denom_a /= c;
            nom_a *= (denom_b/c);
            denom_a *= (nom_b/d);
            // compute result
            MTBDD result = mtbdd_fraction(nom_a, denom_a);
            return result;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_divide type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    return mtbdd_invalid;
}

/**
 * Binary operation Equals (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Boolean, or Integer, or Double.
 * For Integer/Double MTBDD, if either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_equals, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    if (a == mtbdd_false && b == mtbdd_false) return mtbdd_true;
    if (a == mtbdd_true && b == mtbdd_true) return mtbdd_true;
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            int64_t va = *(int64_t*)(&val_a);
            int64_t vb = *(int64_t*)(&val_b);
            if (va == vb) return mtbdd_true;
            return mtbdd_false;
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            if (vval_a == vval_b) return mtbdd_true;
            return mtbdd_false;
        } else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            // both fraction
            uint64_t nom_a = val_a>>32;
            uint64_t nom_b = val_b>>32;
            uint64_t denom_a = val_a&0xffffffff;
            uint64_t denom_b = val_b&0xffffffff;
            if (nom_a == nom_b && denom_a == denom_b) return mtbdd_true;
            return mtbdd_false;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_equals type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    if (a < b) {
        *pa = b;
        *pb = a;
    }
    
    return mtbdd_invalid;
}

/**
 * Binary operation Equals (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Boolean, or Integer, or Double.
 * For Integer/Double MTBDD, if either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_less, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    if (a == mtbdd_false && b == mtbdd_false) return mtbdd_true;
    if (a == mtbdd_true && b == mtbdd_true) return mtbdd_true;
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            int64_t va = *(int64_t*)(&val_a);
            int64_t vb = *(int64_t*)(&val_b);
            if (va < vb) return mtbdd_true;
            return mtbdd_false;
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            if (vval_a < vval_b) return mtbdd_true;
            return mtbdd_false;
        } else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            // both fraction
            uint64_t nom_a = val_a>>32;
            uint64_t nom_b = val_b>>32;
            uint64_t denom_a = val_a&0xffffffff;
            uint64_t denom_b = val_b&0xffffffff;
            return nom_a * denom_b < nom_b * denom_a ? mtbdd_true : mtbdd_false;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_less type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    return mtbdd_invalid;
}

/**
 * Binary operation Equals (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Boolean, or Integer, or Double.
 * For Integer/Double MTBDD, if either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_less_or_equal, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    if (a == mtbdd_false && b == mtbdd_false) return mtbdd_true;
    if (a == mtbdd_true && b == mtbdd_true) return mtbdd_true;
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            int64_t va = *(int64_t*)(&val_a);
            int64_t vb = *(int64_t*)(&val_b);
            return va <= vb ? mtbdd_true : mtbdd_false;
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            if (vval_a <= vval_b) return mtbdd_true;
            return mtbdd_false;
        } else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            // both fraction
            uint64_t nom_a = val_a>>32;
            uint64_t nom_b = val_b>>32;
            uint64_t denom_a = val_a&0xffffffff;
            uint64_t denom_b = val_b&0xffffffff;
            nom_a *= denom_b;
            nom_b *= denom_a;
            return nom_a <= nom_b ? mtbdd_true : mtbdd_false;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_less_or_equal type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    return mtbdd_invalid;
}

/**
 * Binary operation Pow (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Double.
 * For Integer/Double MTBDD, if either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_pow, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            assert(0);
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            return mtbdd_double(pow(vval_a, vval_b));
        } else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            assert(0);
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_pow type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    return mtbdd_invalid;
}

/**
 * Binary operation Mod (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Double.
 * For Integer/Double MTBDD, if either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_mod, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            assert(0);
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            return mtbdd_double(fmod(vval_a, vval_b));
        } else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            assert(0);
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_mod type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    return mtbdd_invalid;
}

/**
 * Binary operation Log (for MTBDDs of same type)
 * Only for MTBDDs where either all leaves are Double.
 * For Integer/Double MTBDD, if either operand is mtbdd_false (not defined),
 * then the result is mtbdd_false (i.e. not defined).
 */
TASK_IMPL_2(MTBDD, mtbdd_op_logxy, MTBDD*, pa, MTBDD*, pb)
{
    MTBDD a = *pa, b = *pb;
    
    mtbddnode_t na = GETNODE(a);
    mtbddnode_t nb = GETNODE(b);
    
    if (mtbddnode_isleaf(na) && mtbddnode_isleaf(nb)) {
        uint64_t val_a = mtbddnode_getvalue(na);
        uint64_t val_b = mtbddnode_getvalue(nb);
        if (mtbddnode_gettype(na) == 0 && mtbddnode_gettype(nb) == 0) {
            assert(0);
        } else if (mtbddnode_gettype(na) == 1 && mtbddnode_gettype(nb) == 1) {
            // both double
            double vval_a = *(double*)&val_a;
            double vval_b = *(double*)&val_b;
            return mtbdd_double(log(vval_a) / log(vval_b));
        } else if (mtbddnode_gettype(na) == 2 && mtbddnode_gettype(nb) == 2) {
            assert(0);
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID && mtbddnode_gettype(nb) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_logxy type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    return mtbdd_invalid;
}

TASK_IMPL_2(MTBDD, mtbdd_op_not_zero, MTBDD, a, size_t, v)
{
    /* We only expect "double" terminals, or false */
    if (a == mtbdd_false) return mtbdd_false;
    if (a == mtbdd_true) return mtbdd_true;
    
    // a != constant
    mtbddnode_t na = GETNODE(a);
    
    if (mtbddnode_isleaf(na)) {
        if (mtbddnode_gettype(na) == 0) {
            return mtbdd_getint64(a) != 0 ? mtbdd_true : mtbdd_false;
        } else if (mtbddnode_gettype(na) == 1) {
            return mtbdd_getdouble(a) != 0.0 ? mtbdd_true : mtbdd_false;
        } else if (mtbddnode_gettype(na) == 2) {
            return mtbdd_getnumer(a) != 0 ? mtbdd_true : mtbdd_false;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			return storm_rational_function_is_zero((storm_rational_function_ptr)mtbdd_getvalue(a)) == 0 ? mtbdd_true : mtbdd_false;
		}
#endif
    }
    
    // Ugly hack to get rid of the error "unused variable v" (because there is no version of uapply without a parameter).
    (void)v;
    
    return mtbdd_invalid;
}

TASK_IMPL_1(MTBDD, mtbdd_not_zero, MTBDD, dd)
{
    return mtbdd_uapply(dd, TASK(mtbdd_op_not_zero), 0);
}

TASK_IMPL_2(MTBDD, mtbdd_op_floor, MTBDD, a, size_t, v)
{
    /* We only expect "double" terminals, or false */
    if (a == mtbdd_false) return mtbdd_false;
    if (a == mtbdd_true) return mtbdd_true;
    
    // a != constant
    mtbddnode_t na = GETNODE(a);
    
    if (mtbddnode_isleaf(na)) {
        if (mtbddnode_gettype(na) == 0) {
            return a;
        } else if (mtbddnode_gettype(na) == 1) {
            MTBDD result = mtbdd_double(floor(mtbdd_getdouble(a)));
            return result;
        } else if (mtbddnode_gettype(na) == 2) {
            MTBDD result = mtbdd_fraction(mtbdd_getnumer(a) / mtbdd_getdenom(a), 1);
            return result;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_floor type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }
    
    // Ugly hack to get rid of the error "unused variable v" (because there is no version of uapply without a parameter).
    (void)v;
    
    return mtbdd_invalid;
}

TASK_IMPL_1(MTBDD, mtbdd_floor, MTBDD, dd)
{
    return mtbdd_uapply(dd, TASK(mtbdd_op_floor), 0);
}

TASK_IMPL_2(MTBDD, mtbdd_op_ceil, MTBDD, a, size_t, v)
{
    /* We only expect "double" terminals, or false */
    if (a == mtbdd_false) return mtbdd_false;
    if (a == mtbdd_true) return mtbdd_true;
    
    // a != constant
    mtbddnode_t na = GETNODE(a);
    
    if (mtbddnode_isleaf(na)) {
        if (mtbddnode_gettype(na) == 0) {
            return a;
        } else if (mtbddnode_gettype(na) == 1) {
            MTBDD result = mtbdd_double(ceil(mtbdd_getdouble(a)));
            return result;
        } else if (mtbddnode_gettype(na) == 2) {
            MTBDD result = mtbdd_fraction(mtbdd_getnumer(a) / mtbdd_getdenom(a) + 1, 1);
            return result;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			printf("ERROR mtbdd_op_ceil type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID");
			assert(0);
		}
#endif
    }

    // Ugly hack to get rid of the error "unused variable v" (because there is no version of uapply without a parameter).
    (void)v;
    
    return mtbdd_invalid;
}

TASK_IMPL_1(MTBDD, mtbdd_ceil, MTBDD, dd)
{
    return mtbdd_uapply(dd, TASK(mtbdd_op_ceil), 0);
}

TASK_IMPL_2(MTBDD, mtbdd_op_bool_to_double, MTBDD, a, size_t, v)
{
    /* We only expect "double" terminals, or false */
    if (a == mtbdd_false) return mtbdd_double(0);
    if (a == mtbdd_true) return mtbdd_double(1.0);
    
    // Ugly hack to get rid of the error "unused variable v" (because there is no version of uapply without a parameter).
    (void)v;
    
    return mtbdd_invalid;
}

TASK_IMPL_1(MTBDD, mtbdd_bool_to_double, MTBDD, dd)
{
    return mtbdd_uapply(dd, TASK(mtbdd_op_bool_to_double), 0);
}

TASK_IMPL_2(MTBDD, mtbdd_op_bool_to_int64, MTBDD, a, size_t, v)
{
    /* We only expect "double" terminals, or false */
    if (a == mtbdd_false) return mtbdd_int64(0);
    if (a == mtbdd_true) return mtbdd_int64(1);
    
    // Ugly hack to get rid of the error "unused variable v" (because there is no version of uapply without a parameter).
    (void)v;
    
    return mtbdd_invalid;
}

TASK_IMPL_1(MTBDD, mtbdd_bool_to_int64, MTBDD, dd)
{
    return mtbdd_uapply(dd, TASK(mtbdd_op_bool_to_int64), 0);
}

/**
 * Calculate the number of satisfying variable assignments according to <variables>.
 */
TASK_IMPL_2(double, mtbdd_non_zero_count, MTBDD, dd, size_t, nvars)
{
    /* Trivial cases */
    if (dd == mtbdd_false) return 0.0;

    mtbddnode_t na = GETNODE(dd);
    
    if (mtbdd_isleaf(dd)) {
        if (mtbddnode_gettype(na) == 0) {
            return mtbdd_getint64(dd) != 0 ? powl(2.0L, nvars) : 0.0;
        } else if (mtbddnode_gettype(na) == 1) {
            return mtbdd_getdouble(dd) != 0 ? powl(2.0L, nvars) : 0.0;
        } else if (mtbddnode_gettype(na) == 2) {
            return mtbdd_getnumer(dd) != 0 ? powl(2.0L, nvars) : 0.0;
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if (mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
			return storm_rational_function_is_zero((storm_rational_function_ptr)mtbdd_getvalue(dd)) == 0 ? powl(2.0L, nvars) : 0.0;
		}
#endif
    }
    
    /* Perhaps execute garbage collection */
    sylvan_gc_test();
    
    union {
        double d;
        uint64_t s;
    } hack;
    
    /* Consult cache */
    if (cache_get3(CACHE_MTBDD_NONZERO_COUNT, dd, 0, nvars, &hack.s)) {
        sylvan_stats_count(CACHE_MTBDD_NONZERO_COUNT);
        return hack.d;
    }
    
    SPAWN(mtbdd_non_zero_count, mtbdd_gethigh(dd), nvars-1);
    double low = CALL(mtbdd_non_zero_count, mtbdd_getlow(dd), nvars-1);
    hack.d = low + SYNC(mtbdd_non_zero_count);
    
    cache_put3(CACHE_MTBDD_NONZERO_COUNT, dd, 0, nvars, hack.s);
    return hack.d;
}

int mtbdd_iszero(MTBDD dd) {
    if (mtbdd_gettype(dd) == 0) {
        return mtbdd_getint64(dd) == 0;
    } else if (mtbdd_gettype(dd) == 1) {
        return mtbdd_getdouble(dd) == 0;
    } else if (mtbdd_gettype(dd) == 2) {
        return mtbdd_getnumer(dd) == 0;
    }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
	else if (mtbdd_gettype(dd) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID) {
		return storm_rational_function_is_zero((storm_rational_function_ptr)mtbdd_getvalue(dd)) == 1 ? 1 : 0;
	}
#endif
    return 0;
}

int mtbdd_isnonzero(MTBDD dd) {
    return mtbdd_iszero(dd) ? 0 : 1;
}

MTBDD
mtbdd_ithvar(uint32_t level) {
    return mtbdd_makenode(level, mtbdd_false, mtbdd_true);
}

TASK_IMPL_2(MTBDD, mtbdd_op_complement, MTBDD, a, size_t, k)
{
    // if a is false, then it is a partial function. Keep partial!
    if (a == mtbdd_false) return mtbdd_false;

    // a != constant
    mtbddnode_t na = GETNODE(a);

    if (mtbddnode_isleaf(na)) {
        if (mtbddnode_gettype(na) == 0) {
            int64_t v = mtbdd_getint64(a);
			if (v == 0) {
				return mtbdd_int64(1);
			} else {
				return mtbdd_int64(0);
			}            
        } else if (mtbddnode_gettype(na) == 1) {
            double d = mtbdd_getdouble(a);
			if (d == 0.0) {
				return mtbdd_double(1.0);
			} else {
				return mtbdd_double(0.0);
			}  
        } else if (mtbddnode_gettype(na) == 2) {
            printf("ERROR: mtbdd_op_complement type FRACTION.\n");
			assert(0);
        }
#if defined(SYLVAN_HAVE_CARL) || defined(STORM_HAVE_CARL)
		else if ((mtbddnode_gettype(na) == SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID)) {
			printf("ERROR: mtbdd_op_complement type SYLVAN_STORM_RATIONAL_FUNCTION_TYPE_ID.\n");
			assert(0);
		}
#endif
    }

    return mtbdd_invalid;
    (void)k; // unused variable
}

TASK_IMPL_3(MTBDD, mtbdd_minExistsRepresentative, MTBDD, a, MTBDD, variables, uint32_t, prev_level) {
	MTBDD zero = mtbdd_false;
    
	/* Maybe perform garbage collection */
    sylvan_gc_test();
	
    /* Cube is guaranteed to be a cube at this point. */
    if (mtbdd_isleaf(a)) {
        if (mtbdd_set_isempty(variables)) {
            return a; // FIXME?
        } else {
            return variables;
        }
    }
	
	mtbddnode_t na = GETNODE(a);
	uint32_t va = mtbddnode_getvariable(na);
	mtbddnode_t nv = GETNODE(variables);
	uint32_t vv = mtbddnode_getvariable(nv);

    /* Abstract a variable that does not appear in a. */
    if (va > vv) {
		MTBDD _v = mtbdd_set_next(variables);
        MTBDD res = CALL(mtbdd_minExistsRepresentative, a, _v, va);
        if (res == mtbdd_invalid) {
            return mtbdd_invalid;
        }
        
        // Fill in the missing variables to make representative unique.
        mtbdd_ref(res);
        MTBDD res1 = mtbdd_ite(mtbdd_ithvar(vv), zero, res);
        if (res1 == mtbdd_invalid) {
            mtbdd_deref(res);
            return mtbdd_invalid;
        }
        mtbdd_deref(res);
       	return res1;
    }
    
	/* TODO: Caching here. */
    /*if ((res = cuddCacheLookup2(manager, Cudd_addMinAbstractRepresentative, f, cube)) != NULL) {
        return(res);
    }*/
    
    
    MTBDD E = mtbdd_getlow(a);
    MTBDD T = mtbdd_gethigh(a);
    
    /* If the two indices are the same, so are their levels. */
    if (va == vv) {
		MTBDD _v = mtbdd_set_next(variables);
        MTBDD res1 = CALL(mtbdd_minExistsRepresentative, E, _v, va);
        if (res1 == mtbdd_invalid) {
            return mtbdd_invalid;
        }
        mtbdd_ref(res1);
        
        MTBDD res2 = CALL(mtbdd_minExistsRepresentative, T, _v, va);
        if (res2 == mtbdd_invalid) {
            mtbdd_deref(res1);
            return mtbdd_invalid;
        }
        mtbdd_ref(res2);
        
        MTBDD left = mtbdd_abstract_min(E, _v);
        if (left == mtbdd_invalid) {
            mtbdd_deref(res1);
			mtbdd_deref(res2);
            return mtbdd_invalid;
        }
        mtbdd_ref(left);
		
        MTBDD right = mtbdd_abstract_min(T, _v);
        if (right == mtbdd_invalid) {
            mtbdd_deref(res1);
			mtbdd_deref(res2);
			mtbdd_deref(left);
            return mtbdd_invalid;
        }
        mtbdd_ref(right);
        
        MTBDD tmp = mtbdd_less_or_equal_as_bdd(left, right);
        if (tmp == mtbdd_invalid) {
            mtbdd_deref(res1);
			mtbdd_deref(res2);
			mtbdd_deref(left);
			mtbdd_deref(right);
            return mtbdd_invalid;
        }
        mtbdd_ref(tmp);
        
        mtbdd_deref(left);
		mtbdd_deref(right);
        
        MTBDD res1Inf = mtbdd_ite(tmp, res1, zero);
        if (res1Inf == mtbdd_invalid) {
            mtbdd_deref(res1);
			mtbdd_deref(res2);
			mtbdd_deref(tmp);
            return mtbdd_invalid;
        }
        mtbdd_ref(res1Inf);
        mtbdd_deref(res1);
        
        MTBDD tmp2 = mtbdd_get_complement(tmp);
        if (tmp2 == mtbdd_invalid) {
			mtbdd_deref(res2);
			mtbdd_deref(left);
			mtbdd_deref(right);
			mtbdd_deref(tmp);
            return mtbdd_invalid;
        }
        mtbdd_ref(tmp2);
        mtbdd_deref(tmp);
        
        MTBDD res2Inf = mtbdd_ite(tmp2, res2, zero);
        if (res2Inf == mtbdd_invalid) {
            mtbdd_deref(res2);
            mtbdd_deref(res1Inf);
            mtbdd_deref(tmp2);
            return mtbdd_invalid;
        }
        mtbdd_ref(res2Inf);
        mtbdd_deref(res2);
        mtbdd_deref(tmp2);

        MTBDD res = (res1Inf == res2Inf) ? mtbdd_ite(mtbdd_ithvar(va), zero, res1Inf) : mtbdd_ite(mtbdd_ithvar(va), res2Inf, res1Inf);

        if (res == mtbdd_invalid) {
            mtbdd_deref(res1Inf);
            mtbdd_deref(res2Inf);
            return mtbdd_invalid;
        }
        mtbdd_ref(res);
        mtbdd_deref(res1Inf);
        mtbdd_deref(res2Inf);
        
		/* TODO: Caching here. */
		//cuddCacheInsert2(manager, Cudd_addMinAbstractRepresentative, f, cube, res);
		
        mtbdd_deref(res);
        return res;
    }
    else { /* if (cuddI(manager,f->index) < cuddI(manager,cube->index)) */
		MTBDD res1 = CALL(mtbdd_minExistsRepresentative, E, variables, va);
        if (res1 == mtbdd_invalid) {
			return mtbdd_invalid;
		}
        mtbdd_ref(res1);
        MTBDD res2 = CALL(mtbdd_minExistsRepresentative, T, variables, va);
        if (res2 == mtbdd_invalid) {
            mtbdd_deref(res1);
            return mtbdd_invalid;
        }
        mtbdd_ref(res2);

        MTBDD res = (res1 == res2) ? mtbdd_ite(mtbdd_ithvar(va), zero, res1) : mtbdd_ite(mtbdd_ithvar(va), res2, res1);
        if (res == mtbdd_invalid) {
            mtbdd_deref(res1);
            mtbdd_deref(res2);
            return mtbdd_invalid;
        }
        mtbdd_deref(res1);
        mtbdd_deref(res2);
		/* TODO: Caching here. */
        //cuddCacheInsert2(manager, Cudd_addMinAbstractRepresentative, f, cube, res);
        return res;
    }
	
	// Prevent unused variable warning
	(void)prev_level;
}

TASK_IMPL_3(MTBDD, mtbdd_maxExistsRepresentative, MTBDD, a, MTBDD, variables, uint32_t, prev_level) {
	(void)variables;
	(void)prev_level;
	return a;	
}
