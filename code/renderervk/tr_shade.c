/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_shade.c

#include "tr_local.h"

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/


/*
==================
R_DrawElements
==================
*/
#ifndef USE_VULKAN
void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	qglDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, indexes );
}
#endif


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t	tess;
#ifndef USE_VULKAN
static qboolean	setArraysOnce;
#endif

/*
=================
R_BindAnimatedImage
=================
*/
static void R_BindAnimatedImage( const textureBundle_t *bundle ) {
	int64_t index;
	double	v;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ( bundle->isScreenMap /*&& backEnd.viewParms.frameSceneNum == 1*/ ) {
		if ( !backEnd.screenMapDone )
			GL_Bind( tr.blackImage );
		else
			vk_update_descriptor( glState.currenttmu + VK_DESC_TEXTURE_BASE, vk.screenMap.color_descriptor );
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_Bind( bundle->image[0] );
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	//v = tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE;
	//index = v;
	//index >>= FUNCTABLE_SIZE2;

	v = tess.shaderTime * bundle->imageAnimationSpeed; // fix for frameloss bug -EC-
	index = v;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}
	index %= bundle->numImageAnimations;

	GL_Bind( bundle->image[ index ] );
}


/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris( const shaderCommands_t *input ) {
#ifdef USE_VULKAN
	uint32_t pipeline;

	if ( r_showtris->integer == 1 && backEnd.drawConsole )
		return;

	if ( tess.numIndexes == 0 )
		return;

	if ( r_fastsky->integer && input->shader->isSky )
		return;

#ifdef USE_VBO
	if ( tess.vboIndex ) {
#ifdef USE_PMLIGHT
		if ( tess.dlightPass )
			pipeline = backEnd.viewParms.portalView == PV_MIRROR ? vk.tris_mirror_debug_red_pipeline : vk.tris_debug_red_pipeline;
		else
#endif
			pipeline = backEnd.viewParms.portalView == PV_MIRROR ? vk.tris_mirror_debug_green_pipeline : vk.tris_debug_green_pipeline;
	} else
#endif
	{
#ifdef USE_PMLIGHT
		if ( tess.dlightPass )
			pipeline = backEnd.viewParms.portalView == PV_MIRROR ? vk.tris_mirror_debug_red_pipeline : vk.tris_debug_red_pipeline;
		else
#endif
			pipeline = backEnd.viewParms.portalView == PV_MIRROR ? vk.tris_mirror_debug_pipeline : vk.tris_debug_pipeline;
	}

	vk_bind_pipeline( pipeline );
	vk_draw_geometry( DEPTH_RANGE_ZERO, qtrue );

#else
	if ( r_showtris->integer == 1 && backEnd.drawConsole )
		return;

	GL_ClientState( 0, CLS_NONE );
	qglDisable( GL_TEXTURE_2D );

	qglColor4f( 1, 1, 1, 1 );

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );

	if ( qglLockArraysEXT ) {
		qglLockArraysEXT( 0, input->numVertexes );
	}

	R_DrawElements( input->numIndexes, input->indexes );

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}

	qglEnable( GL_TEXTURE_2D );

	qglDepthRange( 0, 1 );
#endif
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals( const shaderCommands_t *input ) {
	int		i;
#ifdef USE_VULKAN
#ifdef USE_VBO
	if ( tess.vboIndex )
		return; // must be handled specially
#endif

	GL_Bind( tr.whiteImage );

	tess.numIndexes = 0;
	for ( i = 0; i < tess.numVertexes; i++ ) {
		VectorMA( tess.xyz[i], 2.0, tess.normal[i], tess.xyz[i + tess.numVertexes] );
		tess.indexes[  tess.numIndexes + 0 ] = i;
		tess.indexes[  tess.numIndexes + 1 ] = i + tess.numVertexes;
		tess.numIndexes += 2;
	}
	tess.numVertexes *= 2;
	Com_Memset( tess.svars.colors[0][0].rgba, tr.identityLightByte, tess.numVertexes * sizeof( color4ub_t ) );

	vk_bind_pipeline( vk.normals_debug_pipeline );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_ST0 | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_ZERO, qtrue );
#else
	GL_ClientState( 0, CLS_NONE );

	qglDisable( GL_TEXTURE_2D );
	qglColor4f( 1, 1, 1, 1 );

	qglDepthRange( 0, 0 );	// never occluded

	GL_State( GLS_DEPTHMASK_TRUE );

	for ( i = tess.numVertexes-1; i >= 0; i-- ) {
		VectorMA( tess.xyz[i], 2.0, tess.normal[i], tess.xyz[i*2 + 1] );
		VectorCopy( tess.xyz[i], tess.xyz[i*2] );
	}

	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	if ( qglLockArraysEXT ) {
		qglLockArraysEXT( 0, tess.numVertexes * 2 );
	}

	qglDrawArrays( GL_LINES, 0, tess.numVertexes * 2 );

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}

	qglEnable( GL_TEXTURE_2D );

	qglDepthRange( 0, 1 );
#endif
}


/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state;

#ifdef USE_VBO
	if ( shader->isStaticShader && !shader->remappedShader ) {
		tess.allowVBO = qtrue;
	} else {
		tess.allowVBO = qfalse;
	}
#endif

	if ( shader->remappedShader ) {
		state = shader->remappedShader;
	} else {
		state = shader;
	}

#ifdef USE_PMLIGHT
	if ( tess.fogNum != fogNum ) {
		tess.dlightUpdateParams = qtrue;
	}
#endif

#ifdef USE_TESS_NEEDS_NORMAL
#ifdef USE_PMLIGHT
	tess.needsNormal = state->needsNormal || tess.dlightPass || r_shownormals->integer;
#else
	tess.needsNormal = state->needsNormal || r_shownormals->integer;
#endif
#endif

#ifdef USE_TESS_NEEDS_ST2
	tess.needsST2 = state->needsST2;
#endif

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
#ifdef USE_VK_PBR
	tess.multiDrawPrimitives = 0;
#endif


#ifdef USE_LEGACY_DLIGHTS
	tess.dlightBits = 0;		// will be OR'd in by surface functions
#endif
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if ( tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime ) {
		tess.shaderTime = tess.shader->clampTime;
	}
}


/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
#ifndef USE_VULKAN
static void DrawMultitextured( const shaderCommands_t *input, int stage ) {
	const shaderStage_t *pStage;

	pStage = tess.xstages[ stage ];

	GL_State( pStage->stateBits );

	if ( !setArraysOnce ) {
		R_ComputeColors( 0, tess.svars.colors[0], pStage );
		R_ComputeTexCoords( 0, &pStage->bundle[0] );
		R_ComputeTexCoords( 1, &pStage->bundle[1] );
		GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

		qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[0] );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );

		GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[1] );
	}

	//
	// base
	//
	GL_SelectTexture( 0 );
	R_BindAnimatedImage( &pStage->bundle[0] );

	//
	// lightmap/secondary pass
	//
	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	R_BindAnimatedImage( &pStage->bundle[1] );

	if ( r_lightmap->integer ) {
		GL_TexEnv( GL_REPLACE );
	} else {
		GL_TexEnv( pStage->mtEnv );
	}

	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	//GL_ClientState( 1, CLS_NONE );

	qglDisable( GL_TEXTURE_2D );
	GL_SelectTexture( 0 );
}
#endif


#ifdef USE_LEGACY_DLIGHTS
/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
#ifdef USE_VULKAN
static qboolean ProjectDlightTexture( void ) {
#else
static void ProjectDlightTexture( void ) {
#endif
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
#ifdef USE_VULKAN
	uint32_t pipeline;
	qboolean rebindIndex = qfalse;
#else
	float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	byte	colorArray[SHADER_MAX_VERTEXES][4];
#endif
	glIndex_t hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	scale;
	float	radius;
	float	modulate = 0.0f;
	const dlight_t *dl;

	if ( !backEnd.refdef.num_dlights ) {
#ifdef USE_VULKAN
		return rebindIndex;
#else
		return;
#endif
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definitely doesn't have any of this light
		}

#ifdef USE_VULKAN
		texCoords = (float*)&tess.svars.texcoords[0][0];
		tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];
		colors = tess.svars.colors[0][0].rgba;
#else
		texCoords = texCoordsArray[0];
		colors = colorArray[0];
#endif

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			int		clip = 0;
			vec3_t	dist;

			VectorSubtract( origin, tess.xyz[i], dist );

			backEnd.pc.c_dlightVertexes++;

			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			if ( !r_dlightBacks->integer &&
					// dist . tess.normal[i]
					( dist[0] * tess.normal[i][0] +
					dist[1] * tess.normal[i][1] +
					dist[2] * tess.normal[i][2] ) < 0.0f ) {
				clip = 63;
			} else {
				if ( texCoords[0] < 0.0f ) {
					clip |= 1;
				} else if ( texCoords[0] > 1.0f ) {
					clip |= 2;
				}
				if ( texCoords[1] < 0.0f ) {
					clip |= 4;
				} else if ( texCoords[1] > 1.0f ) {
					clip |= 8;
				}

				// modulate the strength based on the height and color
				if ( dist[2] > radius ) {
					clip |= 16;
					modulate = 0.0f;
				} else if ( dist[2] < -radius ) {
					clip |= 32;
					modulate = 0.0f;
				} else {
					//*((int*)&dist[2]) &= 0x7FFFFFFF;
					dist[2] = fabsf( dist[2] );
					if ( dist[2] < radius * 0.5f ) {
						modulate = 1.0 * 255.0;
					} else {
						modulate = 2.0f * (radius - dist[2]) * scale * 255.0;
					}
				}
			}
			clipBits[i] = clip;
			colors[0] = dl->color[0] * modulate;
			colors[1] = dl->color[1] * modulate;
			colors[2] = dl->color[2] * modulate;
			colors[3] = 255;
		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			glIndex_t a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( numIndexes == 0 ) {
			continue;
		}

#ifndef USE_VULKAN
		GL_ClientState( 1, CLS_NONE );
		GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

		qglTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );
#endif

		GL_Bind( tr.dlightImage );

#ifdef USE_VULKAN
		if ( numIndexes != tess.numIndexes ) {
			// re-bind index buffer for later fog pass
			rebindIndex = qtrue;
		}
		pipeline = vk.dlight_pipelines[dl->additive > 0 ? 1 : 0][tess.shader->cullType][tess.shader->polygonOffset];
		vk_bind_pipeline( pipeline );
		vk_bind_index_ext( numIndexes, hitIndexes );
		vk_bind_geometry( TESS_RGBA0 | TESS_ST0 );
		vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
#else
		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered

		if ( dl->additive ) {
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		} else {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}

		R_DrawElements( numIndexes, hitIndexes );
#endif
		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}

#ifdef USE_VULKAN
	return rebindIndex;
#endif
}

#endif // USE_LEGACY_DLIGHTS

uint32_t vk_append_uniform( const void *uniform, size_t size, uint32_t min_offset );
uint32_t vk_push_uniform( const vkUniform_t *uniform );
#ifdef USE_VK_PBR
uint32_t vk_push_indirect( int count, const void *data );
uint32_t vk_push_uniform_global( const vkUniformGlobal_t *uniform );
#endif
void VK_SetFogParams( vkUniform_t *uniform, int *fogStage );
static vkUniform_t			uniform;
static vkUniformGlobal_t	uniform_global;

/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
#ifdef USE_VULKAN
static void RB_FogPass( qboolean rebindIndex ) {
	uint32_t pipeline = vk.fog_pipelines[tess.shader->fogPass - 1][tess.shader->cullType][tess.shader->polygonOffset];
#ifdef USE_FOG_ONLY
	int fog_stage;

	// fog parameters
	vk_bind_pipeline( pipeline );
	if ( rebindIndex ) {
		vk_bind_index();
	}
	VK_SetFogParams( &uniform, &fog_stage );
	vk_push_uniform( &uniform );
	vk_update_descriptor( VK_DESC_FOG_ONLY, tr.fogImage->descriptor );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
#else
	const fog_t	*fog = tr.world->fogs + tess.fogNum;
	int	i;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		tess.svars.colors[0][i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );
	tess.svars.texcoordPtr[ 0 ] = tess.svars.texcoords[ 0 ];
	GL_Bind( tr.fogImage );

	vk_bind_pipeline( pipeline );
	if ( rebindIndex ) {
		vk_bind_index();
	}
	vk_bind_geometry( TESS_ST0 | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
#endif
#else
static void RB_FogPass( void ) {
	const fog_t	*fog;
	int			i;

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors[0].rgba );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	GL_SelectTexture( 0 );
	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	R_DrawElements( tess.numIndexes, tess.indexes );
#endif
}


/*
===============
R_ComputeColors
===============
*/
void R_ComputeColors( const int b, color4ub_t *dest, const shaderStage_t *pStage )
{
	int		i;

	if ( tess.numVertexes == 0 )
		return;

	//
	// rgbGen
	//
	switch ( pStage->bundle[b].rgbGen )
	{
		case CGEN_IDENTITY:
			Com_Memset( dest, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			Com_Memset( dest, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( ( unsigned char * ) dest );
			break;
		case CGEN_EXACT_VERTEX:
			Com_Memcpy( dest, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dest[i] = pStage->bundle[b].constantColor;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				Com_Memcpy( dest, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					dest[i].rgba[0] = tess.vertexColors[i].rgba[0] * tr.identityLight;
					dest[i].rgba[1] = tess.vertexColors[i].rgba[1] * tr.identityLight;
					dest[i].rgba[2] = tess.vertexColors[i].rgba[2] * tr.identityLight;
					dest[i].rgba[3] = tess.vertexColors[i].rgba[3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					dest[i].rgba[0] = 255 - tess.vertexColors[i].rgba[0];
					dest[i].rgba[1] = 255 - tess.vertexColors[i].rgba[1];
					dest[i].rgba[2] = 255 - tess.vertexColors[i].rgba[2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					dest[i].rgba[0] = ( 255 - tess.vertexColors[i].rgba[0] ) * tr.identityLight;
					dest[i].rgba[1] = ( 255 - tess.vertexColors[i].rgba[1] ) * tr.identityLight;
					dest[i].rgba[2] = ( 255 - tess.vertexColors[i].rgba[2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				const fog_t *fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					dest[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->bundle[b].rgbWave, dest->rgba );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( dest->rgba );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( dest->rgba );
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->bundle[b].alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( ( pStage->bundle[b].rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
			 pStage->bundle[b].rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dest[i].rgba[3] = 255;
			}
		}
		break;
	case AGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			dest[i].rgba[3] = pStage->bundle[b].constantColor.rgba[3];
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->bundle[b].alphaWave, dest->rgba );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( dest->rgba );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( dest->rgba );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( dest->rgba );
		break;
	case AGEN_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			dest[i].rgba[3] = tess.vertexColors[i].rgba[3];
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			dest[i].rgba[3] = 255 - tess.vertexColors[i].rgba[3];
		}
		break;
	case AGEN_PORTAL:
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				unsigned char alpha;
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				len = VectorLength( v ) * tess.shader->portalRangeR;

				if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				dest[i].rgba[3] = alpha;
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->bundle[b].adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( dest->rgba );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( dest->rgba );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( dest->rgba );
			break;
		case ACFF_NONE:
			break;
		}
	}
}


/*
===============
R_ComputeTexCoords
===============
*/
void R_ComputeTexCoords( const int b, const textureBundle_t *bundle ) {
	int	i;
	int tm;
	vec2_t *src, *dst;

	if ( !tess.numVertexes )
		return;

	src = dst = tess.svars.texcoords[b];

	//
	// generate the texture coordinates
	//
	switch ( bundle->tcGen )
	{
	case TCGEN_IDENTITY:
		src = tess.texCoords00;
		break;
	case TCGEN_TEXTURE:
		src = tess.texCoords[0];
		break;
	case TCGEN_LIGHTMAP:
		src = tess.texCoords[1];
		break;
	case TCGEN_VECTOR:
		for ( i = 0 ; i < tess.numVertexes ; i++ ) {
			dst[i][0] = DotProduct( tess.xyz[i], bundle->tcGenVectors[0] );
			dst[i][1] = DotProduct( tess.xyz[i], bundle->tcGenVectors[1] );
		}
		break;
	case TCGEN_FOG:
		RB_CalcFogTexCoords( ( float * ) dst );
		break;
	case TCGEN_ENVIRONMENT_MAPPED:
		RB_CalcEnvironmentTexCoords( ( float * ) dst );
		break;
	case TCGEN_ENVIRONMENT_MAPPED_FP:
		RB_CalcEnvironmentTexCoordsFP( ( float * ) dst, bundle->isScreenMap );
		break;
	case TCGEN_BAD:
		return;
	}

	//
	// alter texture coordinates
	//
	for ( tm = 0; tm < bundle->numTexMods ; tm++ ) {
		switch ( bundle->texMods[tm].type )
		{
		case TMOD_NONE:
			tm = TR_MAX_TEXMODS; // break out of for loop
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentTexCoords( &bundle->texMods[tm].wave, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexCoords( bundle->texMods[tm].scroll, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_SCALE:
			RB_CalcScaleTexCoords( bundle->texMods[tm].scale, (float *) src, (float *) dst );
			src = dst;
			break;

		case TMOD_OFFSET:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dst[i][0] = src[i][0] + bundle->texMods[tm].offset[0];
				dst[i][1] = src[i][1] + bundle->texMods[tm].offset[1];
			}
			src = dst;
			break;

		case TMOD_SCALE_OFFSET:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dst[i][0] = (src[i][0] * bundle->texMods[tm].scale[0] ) + bundle->texMods[tm].offset[0];
				dst[i][1] = (src[i][1] * bundle->texMods[tm].scale[1] ) + bundle->texMods[tm].offset[1];
			}
			src = dst;
			break;

		case TMOD_OFFSET_SCALE:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dst[i][0] = (src[i][0] + bundle->texMods[tm].offset[0]) * bundle->texMods[tm].scale[0];
				dst[i][1] = (src[i][1] + bundle->texMods[tm].offset[1]) * bundle->texMods[tm].scale[1];
			}
			src = dst;
			break;

		case TMOD_STRETCH:
			RB_CalcStretchTexCoords( &bundle->texMods[tm].wave, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexCoords( &bundle->texMods[tm], (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexCoords( bundle->texMods[tm].rotateSpeed, (float *) src, (float *) dst );
			src = dst;
			break;

		default:
			ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle->texMods[tm].type, tess.shader->name );
			break;
		}
	}

	tess.svars.texcoordPtr[ b ] = src;
}

#ifdef USE_VK_PBR
static qboolean vk_is_valid_pbr_surface( const qboolean hasPBR ) {
	if( !vk.pbrActive || !hasPBR )
		return qfalse;

	if ( backEnd.projection2D )
		return qfalse;

	//if ( backEnd.viewParms.portalView == PV_MIRROR )
	//	return qfalse;
	//
	//if ( backEnd.currentEntity ) {
	//	if ( backEnd.currentEntity != &tr.worldEntity )
	//		return qfalse;
	//}

	return qtrue;
}

static qboolean ShaderRequiresCPUDeforms( const shader_t *shader ) {

	// only do this for ghoul2
	//if ( tess.vbo_model && tess.surfType == SF_IQM  ){

		if ( shader->numDeforms > 1 )
			return qtrue;

		deformStage_t *ds;

		if ( shader->numDeforms == 1 ) {
			// only support the first one
			deformStage_t *ds = &tess.shader->deforms[ 0 ];

			switch ( ds->deformation ) {
				case DEFORM_NONE:
				case DEFORM_NORMALS:
				case DEFORM_WAVE:
				case DEFORM_BULGE:
				case DEFORM_MOVE:
				case DEFORM_PROJECTION_SHADOW:
					return qfalse;
				default:
					return qtrue;
			}
		}

		assert( shader->numDeforms == 0 );

		return qfalse;
	//}

	return qtrue;
}

static void vk_compute_tex_mods( const textureBundle_t *bundle, float *outMatrix, float *outOffTurb ) {
	int tm;
	float matrix[6], currentmatrix[6];

	matrix[0] = 1.0f; matrix[2] = 0.0f; matrix[4] = 0.0f;
	matrix[1] = 0.0f; matrix[3] = 1.0f; matrix[5] = 0.0f;

	currentmatrix[0] = 1.0f; currentmatrix[2] = 0.0f; currentmatrix[4] = 0.0f;
	currentmatrix[1] = 0.0f; currentmatrix[3] = 1.0f; currentmatrix[5] = 0.0f;

	outMatrix[0] = 1.0f; outMatrix[2] = 0.0f;
	outMatrix[1] = 0.0f; outMatrix[3] = 1.0f;

	outOffTurb[0] = 0.0f; outOffTurb[1] = 0.0f; outOffTurb[2] = 0.0f; outOffTurb[3] = 0.0f;

	for ( tm = 0; tm < bundle->numTexMods ; tm++ ) {
		switch ( bundle->texMods[tm].type )
		{
			
		case TMOD_NONE:
			tm = TR_MAX_TEXMODS;		// break out of for loop
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentFactors(&bundle->texMods[tm].wave, &outOffTurb[2], &outOffTurb[3]);
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexMatrix( backEnd.currentEntity->e.shaderTexCoord, matrix );
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexMatrix( bundle->texMods[tm].translate, matrix );
			break;

		case TMOD_SCALE:
			RB_CalcScaleTexMatrix( bundle->texMods[tm].translate, matrix );
			break;
		
		case TMOD_STRETCH:
			RB_CalcStretchTexMatrix( &bundle->texMods[tm].wave,  matrix );
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexMatrix( &bundle->texMods[tm], matrix );
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexMatrix( bundle->texMods[tm].translate[0], matrix );
			break;

		default:
			ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle->texMods[tm].type, tess.shader->name );
			break;
		}

		switch ( bundle->texMods[tm].type )
		{	
		case TMOD_NONE:
		case TMOD_TURBULENT:
		default:
			break;

		case TMOD_ENTITY_TRANSLATE:
		case TMOD_SCROLL:
		case TMOD_SCALE:
		case TMOD_STRETCH:
		case TMOD_TRANSFORM:
		case TMOD_ROTATE:
			outMatrix[0] = matrix[0] * currentmatrix[0] + matrix[2] * currentmatrix[1];
			outMatrix[1] = matrix[1] * currentmatrix[0] + matrix[3] * currentmatrix[1];

			outMatrix[2] = matrix[0] * currentmatrix[2] + matrix[2] * currentmatrix[3];
			outMatrix[3] = matrix[1] * currentmatrix[2] + matrix[3] * currentmatrix[3];

			outOffTurb[0] = matrix[0] * currentmatrix[4] + matrix[2] * currentmatrix[5] + matrix[4];
			outOffTurb[1] = matrix[1] * currentmatrix[4] + matrix[3] * currentmatrix[5] + matrix[5];

			currentmatrix[0] = outMatrix[0];
			currentmatrix[1] = outMatrix[1];
			currentmatrix[2] = outMatrix[2];
			currentmatrix[3] = outMatrix[3];
			currentmatrix[4] = outOffTurb[0];
			currentmatrix[5] = outOffTurb[1];
			break;
		}
	}
}

static void vk_compute_tex_coords( const textureBundle_t *bundle, vktcMod_t *tcMod, vktcGen_t *tcGen ) {
	vk_compute_tex_mods( bundle, tcMod->matrix, tcMod->offTurb ); 

	tcGen->type = bundle->tcGen;
	
	if ( bundle->tcGen == TCGEN_VECTOR )
	{
		VectorCopy( bundle->tcGenVectors[0], tcGen->vector0 );
		VectorCopy( bundle->tcGenVectors[1], tcGen->vector1 );
	}
}

static void vk_compute_colors( const int b, const shaderStage_t *pStage ){	
	//if ( backEnd.currentEntity->e.renderfx & RF_VOLUMETRIC ) 
	//	return;

	float *baseColor, *vertColor;

	int rgbGen = pStage->bundle[b].rgbGen;
	int alphaGen = pStage->bundle[b].alphaGen;

	baseColor = (float*)uniform_global.bundle[b].baseColor;
	vertColor = (float*)uniform_global.bundle[b].vertColor;

	baseColor[0] = baseColor[1] = baseColor[2] = baseColor[3] = 1.0f;  	
   	vertColor[0] = vertColor[1] = vertColor[2] = vertColor[3] = 0.0f;

	switch ( rgbGen) {
		case CGEN_IDENTITY_LIGHTING: 
			baseColor[0] = baseColor[1] = baseColor[2] = tr.identityLight;
			break;
		case CGEN_EXACT_VERTEX:
			baseColor[0] = baseColor[1] = baseColor[2] = baseColor[3] = 0.0f;
			vertColor[0] = vertColor[1] = vertColor[2] = vertColor[3] = 1.0f;
			break;
		case CGEN_CONST:
			baseColor[0] = pStage->bundle[b].constantColor.rgba[0] / 255.0f;
			baseColor[1] = pStage->bundle[b].constantColor.rgba[1] / 255.0f;
			baseColor[2] = pStage->bundle[b].constantColor.rgba[2] / 255.0f;
			baseColor[3] = pStage->bundle[b].constantColor.rgba[3] / 255.0f;
			break;
		case CGEN_VERTEX:
			baseColor[0] = baseColor[1] = baseColor[2] = baseColor[3] = 0.0f;
			vertColor[0] = vertColor[1] = vertColor[2] = tr.identityLight;
			vertColor[3] = 1.0f;
			break;
		case CGEN_ONE_MINUS_VERTEX:
			baseColor[0] = baseColor[1] = baseColor[2] = tr.identityLight;
			vertColor[0] = vertColor[1] = vertColor[2] = -tr.identityLight;
			break;
		case CGEN_FOG:
			{
				fog_t *fog = tr.world->fogs + tess.fogNum;
				Vector4Copy(fog->color, baseColor);
			}
			break;
		case CGEN_WAVEFORM:
			baseColor[0] = baseColor[1] = baseColor[2] = RB_CalcWaveColorSingle( &pStage->bundle[b].rgbWave );
			break;
		case CGEN_ENTITY:
			if ( backEnd.currentEntity )
			{
				baseColor[0] = ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[0] / 255.0f;
				baseColor[1] = ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[1] / 255.0f;
				baseColor[2] = ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[2] / 255.0f;
				baseColor[3] = ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[3] / 255.0f;

				//vertColor[0] = vertColor[1] = vertColor[2] = tr.identityLight;
				//vertColor[3] = 1.0f;

				if ( alphaGen == AGEN_IDENTITY && backEnd.currentEntity->e.shader.rgba[3] == 255 )
					alphaGen = AGEN_SKIP;
			}
			break;
		case CGEN_ONE_MINUS_ENTITY:
			if ( backEnd.currentEntity )
			{
				baseColor[0] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[0] / 255.0f;
				baseColor[1] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[1] / 255.0f;
				baseColor[2] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[2] / 255.0f;
				baseColor[3] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[3] / 255.0f;
			}
			break;
		case CGEN_IDENTITY:
		case CGEN_LIGHTING_DIFFUSE:
		case CGEN_BAD:
			break;
		default:
			break;
	}

	switch ( alphaGen ) {
		case AGEN_SKIP:
			break;
		case AGEN_CONST:
			if ( rgbGen != CGEN_CONST ) {
				baseColor[3] = pStage->bundle[b].constantColor.rgba[3] / 255.0f;
				vertColor[3] = 0.0f;
			}
			break;
		case AGEN_WAVEFORM:
			baseColor[3] = RB_CalcWaveAlphaSingle( &pStage->bundle[b].alphaWave );
			vertColor[3] = 0.0f;
			break;
		case AGEN_ENTITY:
			if ( backEnd.currentEntity )
				baseColor[3] = ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[3] / 255.0f;

			vertColor[3] = 0.0f;
			break;
		case AGEN_ONE_MINUS_ENTITY:
			if ( backEnd.currentEntity )
				baseColor[3] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shader.rgba)[3] / 255.0f;

			vertColor[3] = 0.0f;
			break;
		case AGEN_VERTEX:
			if ( rgbGen != CGEN_VERTEX ) {
				baseColor[3] = 0.0f;
				vertColor[3] = 1.0f;			
			}
			break;
		case AGEN_ONE_MINUS_VERTEX:
			baseColor[3] = 1.0f;
			vertColor[3] = -1.0f;
			break;
		case AGEN_IDENTITY:
		case AGEN_LIGHTING_SPECULAR:
		case AGEN_PORTAL:
			// done entirely in vertex program
			baseColor[3] = 1.0f;
			vertColor[3] = 0.0f;
			break;
		default:
			break;
	}
#if 0
	if ( backEnd.currentEntity && backEnd.currentEntity->e.renderfx & RF_FORCE_ENT_ALPHA ) {
		baseColor[3] = backEnd.currentEntity->e.shaderRGBA[3] / 255.0f; 
		vertColor[3] = 0.0f;
	}

	// multiply color by overbrightbits if this isn't a blend
	if ( tr.overbrightBits 
	 && !( ( pStage->stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_DST_COLOR )
	 && !( ( pStage->stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE_MINUS_DST_COLOR )
	 && !( ( pStage->stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_SRC_COLOR )
	 && !( ( pStage->stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) )
	{
		float scale = 1 << tr.overbrightBits;

		baseColor[0] *= scale;
		baseColor[1] *= scale;
		baseColor[2] *= scale;
		vertColor[0] *= scale;
		vertColor[1] *= scale;
		vertColor[2] *= scale;
	}
#endif

	uniform_global.bundle[b].rgbGen = (uint32_t)rgbGen;
	uniform_global.bundle[b].alphaGen = (uint32_t)alphaGen;

	if ( alphaGen == AGEN_PORTAL )
		uniform_global.portalRange = tess.shader->portalRange;
}

static void vk_compute_deform( void ) {
	int		type = DEFORM_NONE;
	int		waveFunc = GF_NONE;
	vkDeform_t	*info;

	info = &uniform_global.deform;

	Com_Memset( info + 0, 0, sizeof(float) * 12 );

	//if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE2 ) {
	//	info->type = (float)DEFORM_DISINTEGRATION;
	//	return;
	//}
	deformStage_t *ds;

	if ( tess.shader->numDeforms && !ShaderRequiresCPUDeforms( tess.shader ) ) {
		// only support the first one
		ds = &tess.shader->deforms[ 0 ];

		switch ( ds->deformation ) {
			case DEFORM_WAVE:
				type = DEFORM_WAVE;
				waveFunc = ds->deformationWave.func;

				info->base = ds->deformationWave.base;
				info->amplitude = ds->deformationWave.amplitude;
				info->phase = ds->deformationWave.phase;
				info->frequency = ds->deformationWave.frequency;
				info->vector[0] = ds->deformationSpread;
				info->vector[1] = 0.0f;
				info->vector[2] = 0.0f;
				break;
			case DEFORM_BULGE:
				type = DEFORM_BULGE;

				info->base = 0.0f;
				info->amplitude = ds->bulgeHeight; // amplitude
				info->phase = ds->bulgeWidth;  // phase
				info->frequency = ds->bulgeSpeed;  // frequency
				info->vector[0] = 0.0f;
				info->vector[1] = 0.0f;
				info->vector[2] = 0.0f;

				if ( ds->bulgeSpeed == 0.0f && ds->bulgeWidth == 0.0f )
					type = DEFORM_BULGE_UNIFORM;

				break;
			case DEFORM_MOVE:
				type = DEFORM_MOVE;
				waveFunc = ds->deformationWave.func;

				info->base = ds->deformationWave.base;
				info->amplitude = ds->deformationWave.amplitude;
				info->phase = ds->deformationWave.phase;
				info->frequency = ds->deformationWave.frequency;
				info->vector[0] = ds->moveVector[0];
				info->vector[1] = ds->moveVector[1];
				info->vector[2] = ds->moveVector[2];
				break;
			case DEFORM_NORMALS:
				type = DEFORM_NORMALS;

				info->base = 0.0f;
				info->amplitude = ds->deformationWave.amplitude; // amplitude
				info->phase = 0.0f;  // phase
				info->frequency = ds->deformationWave.frequency;  // frequency
				info->vector[0] = 0.0f;
				info->vector[1] = 0.0f;
				info->vector[2] = 0.0f;
				break;
			case DEFORM_PROJECTION_SHADOW:
				type = DEFORM_PROJECTION_SHADOW;

				info->base = backEnd.or.axis[0][2];
				info->amplitude = backEnd.or.axis[1][2];
				info->phase = backEnd.or.axis[2][2];
				info->frequency = backEnd.or.origin[2] - backEnd.currentEntity->e.shadowPlane;

				vec3_t lightDir = { 0 };
				//VectorCopy( backEnd.currentEntity->modelLightDir, lightDir );
				lightDir[2] = 0.0f;
				VectorNormalize( lightDir );
				VectorSet( lightDir, lightDir[0] * 0.3f, lightDir[1] * 0.3f, 1.0f );

				info->vector[0] = lightDir[0];
				info->vector[1] = lightDir[1];
				info->vector[2] = lightDir[2];
				break;
			default:
				break;
		}
	}

	if ( type != DEFORM_NONE ) {
		info->time = tess.shaderTime;
		info->type = type;
		info->func = waveFunc;	
	}
}
#endif

/*
** RB_IterateStagesGeneric
*/
#ifdef USE_VULKAN
static void RB_IterateStagesGeneric( const shaderCommands_t *input, qboolean fogCollapse )
#else
static void RB_IterateStagesGeneric( const shaderCommands_t *input )
#endif
{
	const shaderStage_t *pStage;
	int tess_flags;
	int stage, i;

#ifdef USE_VULKAN
	uint32_t pipeline;
	int fog_stage;
	qboolean pushUniform;

#ifdef USE_VK_PBR
	qboolean is_pbr_surface;
	qboolean is_mdv_vbo;

	is_mdv_vbo = qfalse;

	if ( tess.vbo_model ) {
#ifdef USE_VBO_MDV
		is_mdv_vbo = (qboolean)( tess.surfType == SF_VBO_MDVMESH );
#endif
	}
#endif

	vk_bind_index();

	tess_flags = input->shader->tessFlags;

	pushUniform = qfalse;

#ifdef USE_FOG_COLLAPSE
	if ( fogCollapse ) {
		VK_SetFogParams( &uniform, &fog_stage );
		VectorCopy( backEnd.or.viewOrigin, uniform.eyePos );
		vk_update_descriptor( VK_DESC_FOG_COLLAPSE, tr.fogImage->descriptor );
		pushUniform = qtrue;
	} else
#endif
	{
		fog_stage = 0;
		if ( tess_flags & TESS_VPOS ) {
			VectorCopy( backEnd.or.viewOrigin, uniform.eyePos );
			tess_flags &= ~TESS_VPOS;
			pushUniform = qtrue;
		}
	}

#ifdef USE_VK_PBR
	Com_Memset( &uniform_global, 0, sizeof(uniform_global) );

	if ( backEnd.currentEntity != &tr.worldEntity ) 
		vk_compute_deform();

	is_pbr_surface = vk_is_valid_pbr_surface( tess.shader->hasPBR );

	if ( is_pbr_surface ) {
		pushUniform = qtrue;
	}
#endif
#endif // USE_VULKAN

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		pStage = tess.xstages[ stage ];
		if ( !pStage )
			break;

#ifdef USE_VBO
		tess.vboStage = stage;
#endif

#ifdef USE_VULKAN
		tess_flags |= pStage->tessFlags;

		for ( i = 0;  i < pStage->numTexBundles; i++ ) {
			if ( pStage->bundle[i].image[0] != NULL ) {
				GL_SelectTexture( i );
				R_BindAnimatedImage( &pStage->bundle[i] );

				if ( tess_flags & (TESS_ENT0 << i) && backEnd.currentEntity ) {
					uniform.ent.color[i][0] = backEnd.currentEntity->e.shader.rgba[0] / 255.0;
					uniform.ent.color[i][1] = backEnd.currentEntity->e.shader.rgba[1] / 255.0;
					uniform.ent.color[i][2] = backEnd.currentEntity->e.shader.rgba[2] / 255.0;
					uniform.ent.color[i][3] = pStage->bundle[i].alphaGen == AGEN_IDENTITY ? 1.0 : (backEnd.currentEntity->e.shader.rgba[3] / 255.0);
					pushUniform = qtrue;
				}
#ifdef USE_VK_PBR
				if ( tess.vbo_model ) {
					vk_compute_colors( i, pStage );

					vk_compute_tex_coords( &pStage->bundle[i], &uniform_global.bundle[i].tcMod, &uniform_global.bundle[i].tcGen );
					uniform_global.bundle[i].numTexMods = pStage->bundle[i].numTexMods;

					pushUniform = qtrue;
					continue;
				}
#endif

				if ( tess_flags & ( TESS_ST0 << i ) ) {
					R_ComputeTexCoords( i, &pStage->bundle[i] );
				}
				if ( tess_flags & ( TESS_RGBA0 << i ) ) {
					R_ComputeColors( i, tess.svars.colors[i], pStage );
				}

			}
		}
#ifndef USE_VK_PBR
		if ( pushUniform ) {
			pushUniform = qfalse;
			vk_push_uniform( &uniform );
		}
#endif
		GL_SelectTexture( 0 );

		if ( r_lightmap->integer && pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE ) {
			//GL_SelectTexture( 0 );
			GL_Bind( tr.whiteImage ); // replace diffuse texture with a white one thus effectively render only lightmap
		}

		if ( backEnd.viewParms.portalView == PV_MIRROR ) {
			pipeline = pStage->vk_mirror_pipeline[fog_stage];
		} else {
			pipeline = pStage->vk_pipeline[fog_stage];
		}

#ifdef USE_VK_PBR
		Vk_Pipeline_Def	def;
		vk_get_pipeline_def( pipeline, &def );


		//if ( is_pbr_surface ) 
		{
			Vector4Copy( pStage->normalScale, uniform_global.normalScale );
			Vector4Copy( pStage->specularScale, uniform_global.specularScale );

			if ( def.vk_light_flags )
				vk_update_descriptor( VK_DESC_PBR_BRDFLUT, vk.brdflut_image_descriptor );
				
			if ( pStage->vk_pbr_flags & PBR_HAS_NORMALMAP )
				vk_update_descriptor( VK_DESC_PBR_NORMAL, pStage->normalMap->descriptor );

			if ( pStage->vk_pbr_flags & PBR_HAS_PHYSICALMAP || pStage->vk_pbr_flags & PBR_HAS_SPECULARMAP )
				vk_update_descriptor( VK_DESC_PBR_PHYSICAL, pStage->physicalMap->descriptor );
			else
			{
				vk_update_descriptor(VK_DESC_PBR_PHYSICAL, tr.whiteImage->descriptor);

				uniform_global.specularScale[0] = 0.0f;
				uniform_global.specularScale[2] =
				uniform_global.specularScale[3] = 1.0f;
				uniform_global.specularScale[1] = 0.5f;
			}

			if ( vk.useFastLight || (!tr.numCubemaps || backEnd.viewParms.targetCube != NULL) ) {
				vk_update_descriptor( VK_DESC_PBR_CUBEMAP, tr.emptyCubemap->descriptor );
				//vk_update_descriptor( 10, tr.emptyCubemap->descriptor ); // irradiance is currently unused
			}
			else { 
				// use the first cubemap index, indexes are not assigned per surface yet
				vk_update_descriptor( VK_DESC_PBR_CUBEMAP, tr.cubemaps[0].prefiltered_image->descriptor );
				//vk_update_descriptor( 10, tr.cubemaps[0].irradiance_image->descriptor ); // irradiance is currently unused
			}

			#ifdef HDR_DELUXE_LIGHTMAP
				// aparently lightmap is not always in bundle 1 ..
				// should probably fix this in collapseMuklitexture
				if ( def.vk_light_flags & LIGHTDEF_USE_LIGHTMAP && def.vk_pbr_flags & PBR_HAS_DELUXEMAP0 )
					vk_update_descriptor(  VK_DESC_PBR_DELUXE, pStage->bundle[0].deluxeMap->descriptor );

				else if ( def.vk_light_flags & LIGHTDEF_USE_LIGHTMAP && def.vk_pbr_flags & PBR_HAS_DELUXEMAP1 )
					vk_update_descriptor(  VK_DESC_PBR_DELUXE, pStage->bundle[1].deluxeMap->descriptor );

				else
					vk_update_descriptor(  VK_DESC_PBR_DELUXE, tr.whiteImage->descriptor );
			#endif
		}

		vk_push_uniform_global( &uniform_global );

		if ( backEnd.currentEntity ) 
		{
			if ( backEnd.currentEntity == &backEnd.entity2D ) 
			{
			}
			else if ( backEnd.currentEntity == &tr.worldEntity ) 
			{
				vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING] = vk.cmd->entity_ubo_offset[REFENTITYNUM_WORLD];
			}
			else 
			{
				const int refEntityNum = backEnd.currentEntity - backEnd.refdef.entities;

				vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING] = vk.cmd->entity_ubo_offset[refEntityNum];
			}

			def.vbo_mdv = is_mdv_vbo;
			pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}

		if ( !is_pbr_surface && pStage->vk_pbr_flags ) {
			def.vk_pbr_flags = 0;
			pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
#endif

		vk_bind_pipeline( pipeline );
		vk_bind_geometry( tess_flags );

#ifdef USE_VK_PBR
		if ( tess.vbo_model )
		{
#ifdef USE_VBO_MDV_INDIRECT
			if ( tess.multiDrawPrimitives ) 
			{
				// draw indexed indirect
				if ( tess.multiDrawPrimitives > 1 ) 
				{
					uint32_t j, offset;
					size_t *index;

					vk.cmd->indirect.numDraws = tess.multiDrawPrimitives;

					for ( j = 0; j < tess.multiDrawPrimitives; j++ ) 
					{
						VkDrawIndexedIndirectCommand indirectCmd = { 0 };

						index = (size_t*)tess.multiDrawFirstIndex + j;
						indirectCmd.indexCount = tess.multiDrawNumIndexes[j];
						indirectCmd.instanceCount = 1;
						indirectCmd.firstIndex = (uint32_t)(*index);
						indirectCmd.vertexOffset = 0;
						indirectCmd.firstInstance = 0;

						offset = vk_push_indirect( 1, &indirectCmd );

						if ( j  == 0 )
							vk.cmd->indirect.offset = offset;
					}
				}
				else{
					vk.cmd->indexed.num_indexes = tess.multiDrawNumIndexes[0];
					vk.cmd->indexed.index_offset = (glIndex_t)(size_t)(tess.multiDrawFirstIndex[0]) * sizeof(uint32_t);
				}
			}
#else
#ifdef USE_VBO_MDV
			if ( is_mdv_vbo )
			{
				vk.cmd->indexed.num_indexes = tess.vbo_mdv_surf[MDV_CURRENT_FRAME].num_indexes;
				vk.cmd->indexed.index_offset = (glIndex_t)(size_t)(tess.vbo_mdv_surf[MDV_CURRENT_FRAME].index_offset) * sizeof(uint32_t);
			}
#endif
#endif
		}
		if ( pushUniform ) {
			pushUniform = qfalse;
			vk_push_uniform( &uniform );
		}
#endif

		vk_draw_geometry( tess.depthRange, qtrue );

		if ( pStage->depthFragment ) {
			if ( backEnd.viewParms.portalView == PV_MIRROR )
				pipeline = pStage->vk_mirror_pipeline_df;
			else
				pipeline = pStage->vk_pipeline_df;
			vk_bind_pipeline( pipeline );
			vk_draw_geometry( tess.depthRange, qtrue );
		}
#else
		R_ComputeColors( 0, tess.svars.colors[0].rgba, pStage );

		R_ComputeTexCoords( 0, &pStage->bundle[0] );

		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != NULL )
		{
			DrawMultitextured( input, stage );
		}
		else
		{
			if ( !setArraysOnce )
			{
				R_ComputeTexCoords( 0, &pStage->bundle[0] );
				R_ComputeColors( 0, tess.svars.colors[0], pStage );

				GL_ClientState( 1, CLS_NONE );
				GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

				qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[0] );
				qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );
			}

			//
			// set state
			//
			R_BindAnimatedImage( &pStage->bundle[0] );

			GL_State( pStage->stateBits );

			//
			// draw
			//
			R_DrawElements( input->numIndexes, input->indexes );
		}
#endif

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].lightmap != LIGHTMAP_INDEX_NONE || pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE ) )
			break;

		tess_flags = 0;
	}

#ifdef USE_VULKAN
	if ( pushUniform ) {
		vk_push_uniform( &uniform );
	}
	if ( tess_flags ) // fog-only shaders?
		vk_bind_geometry( tess_flags );
#endif
}


#ifdef USE_VULKAN

void VK_SetFogParams( vkUniform_t *uniform, int *fogStage )
{
	if ( tess.fogNum && tess.shader->fogPass ) {
		const fogProgramParms_t *fp = RB_CalcFogProgramParms();
		// vertex data
		Vector4Copy( fp->fogDistanceVector, uniform->fogDistanceVector );
		Vector4Copy( fp->fogDepthVector, uniform->fogDepthVector );
		uniform->fogEyeT[0] = fp->eyeT;
		if ( fp->eyeOutside ) {
			uniform->fogEyeT[1] = 0.0; // fog eye out
		} else {
			uniform->fogEyeT[1] = 1.0; // fog eye in
		}
		// fragment data
		Vector4Copy( fp->fogColor, uniform->fogColor );
		*fogStage = 1;
	} else {
		*fogStage = 0;
	}
}


#ifdef USE_PMLIGHT
static void VK_SetLightParams( vkUniform_t *uniform, const dlight_t *dl ) {
	float radius;

#ifdef USE_VULKAN
	if ( !glConfig.deviceSupportsGamma && !vk.fboActive )
#else
	if ( !glConfig.deviceSupportsGamma )
#endif
		VectorScale( dl->color, 2 * powf( r_intensity->value, r_gamma->value ), uniform->light.color);
	else
		VectorCopy( dl->color, uniform->light.color );

	radius = dl->radius;

	// vertex data
	VectorCopy( backEnd.or.viewOrigin, uniform->eyePos ); uniform->eyePos[3] = 0.0f;
	VectorCopy( dl->transformed, uniform->light.pos ); uniform->light.pos[3] = 0.0f;

	// fragment data
	uniform->light.color[3] = 1.0f / Square( radius );

	if ( dl->linear )
	{
		vec4_t ab;
		VectorSubtract( dl->transformed2, dl->transformed, ab );
		ab[3] = 1.0f / DotProduct( ab, ab );
		Vector4Copy( ab, uniform->light.vector );
	}
}
#endif

uint32_t vk_append_uniform( const void *uniform, size_t size, uint32_t min_offset ) {
	const uint32_t offset = PAD(vk.cmd->vertex_buffer_offset, (VkDeviceSize)vk.uniform_alignment);

	if ( offset + min_offset > vk.geometry_buffer_size )
		return ~0U;

	Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, uniform, size );
	vk.cmd->vertex_buffer_offset = offset + min_offset;

	return offset;
}

uint32_t vk_push_uniform( const vkUniform_t *uniform ) {
	const uint32_t offset = vk_append_uniform( uniform, sizeof(*uniform), (VkDeviceSize)vk.uniform_item_size );

	vk_reset_descriptor( VK_DESC_UNIFORM );
	vk_update_descriptor( VK_DESC_UNIFORM, vk.cmd->uniform_descriptor );
	vk_update_descriptor_offset( VK_DESC_UNIFORM_MAIN_BINDING, offset );
	vk_update_descriptor_offset( VK_DESC_UNIFORM_CAMERA_BINDING, vk.cmd->camera_ubo_offset );
	//vk_update_descriptor_offset( VK_DESC_UNIFORM_ENTITY_BINDING, vk.cmd->entity_ubo_offset );

	return offset;
}

#ifdef USE_VK_PBR
uint32_t vk_push_uniform_global( const vkUniformGlobal_t *uniform ) {	
	const uint32_t offset = PAD(vk.cmd->vertex_buffer_offset, (VkDeviceSize)vk.uniform_alignment);

	if ( offset + vk.uniform_global_item_size > vk.geometry_buffer_size )
		return ~0U;

	Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, uniform, sizeof(*uniform) );
	vk.cmd->vertex_buffer_offset = offset + vk.uniform_global_item_size;

	vk_update_descriptor_offset( VK_DESC_UNIFORM_GLOBAL_BINDING, offset );

	return 0;
}

uint32_t vk_push_indirect( int count, const void *data ) {
	const uint32_t offset = vk.cmd->indirect_buffer_offset;	// no alignment for indirect buffer?
	const uint32_t size = count * sizeof(VkDrawIndexedIndirectCommand);

	if (offset + size > vk.indirect_buffer_size) {
		// schedule geometry buffer resize
		vk.indirect_buffer_size_new = log2pad(offset + size, 1);
		Com_Printf("resize"); //hmmm
	}
	else {
		Com_Memcpy(vk.cmd->indirect_buffer_ptr + offset, data, size);
		vk.cmd->indirect_buffer_offset = (VkDeviceSize)offset + size;
	}

	return offset;
}
#endif

#ifdef USE_PMLIGHT
void VK_LightingPass( void )
{
	static uint32_t uniform_offset;
	static int fog_stage;
	uint32_t pipeline;
	const shaderStage_t *pStage;
	cullType_t cull;
	int abs_light;

	if ( tess.shader->lightingStage < 0 )
		return;

	pStage = tess.xstages[ tess.shader->lightingStage ];

	// we may need to update programs for fog transitions
	if ( tess.dlightUpdateParams ) {

		// fog parameters
		VK_SetFogParams( &uniform, &fog_stage );
		// light parameters
		VK_SetLightParams( &uniform, tess.light );

		uniform_offset = vk_push_uniform( &uniform );

		tess.dlightUpdateParams = qfalse;
	}

	if ( uniform_offset == ~0 )
		return; // no space left...

	cull = tess.shader->cullType;
	if ( backEnd.viewParms.portalView == PV_MIRROR ) {
		switch ( cull ) {
			case CT_FRONT_SIDED: cull = CT_BACK_SIDED; break;
			case CT_BACK_SIDED: cull = CT_FRONT_SIDED; break;
			default: break;
		}
	}

	abs_light = /* (pStage->stateBits & GLS_ATEST_BITS) && */ (cull == CT_TWO_SIDED) ? 1 : 0;

	if ( fog_stage )
		vk_update_descriptor( VK_DESC_FOG_DLIGHT, tr.fogImage->descriptor );

	if ( tess.light->linear )
		pipeline = vk.dlight1_pipelines_x[cull][tess.shader->polygonOffset][fog_stage][abs_light];
	else
		pipeline = vk.dlight_pipelines_x[cull][tess.shader->polygonOffset][fog_stage][abs_light];

	GL_SelectTexture( 0 );
	R_BindAnimatedImage( &pStage->bundle[ tess.shader->lightingBundle ] );

#ifdef USE_VBO
	if ( tess.vboIndex == 0 )
#endif
	{
		R_ComputeTexCoords( tess.shader->lightingBundle, &pStage->bundle[ tess.shader->lightingBundle ] );
	}

	vk_bind_pipeline( pipeline );
	vk_bind_index();
	vk_bind_lighting( tess.shader->lightingStage, tess.shader->lightingBundle );
	vk_draw_geometry( tess.depthRange, qtrue );
}
#endif // USE_PMLIGHT

void RB_StageIteratorGeneric( void )
{
#ifdef USE_VULKAN
	qboolean rebindIndex = qfalse;
#endif
	qboolean fogCollapse = qfalse;

#ifdef USE_VBO
	if ( tess.vboIndex != 0 ) {
		VBO_PrepareQueues();
		tess.vboStage = 0;
	} else
#endif
	RB_DeformTessGeometry();

#ifdef USE_PMLIGHT
	if ( tess.dlightPass ) {
		VK_LightingPass();
		return;
	}
#endif

#ifdef USE_FOG_COLLAPSE
	fogCollapse = tess.fogNum && tess.shader->fogPass && tess.shader->fogCollapse;
#endif

	// call shader function
	RB_IterateStagesGeneric( &tess, fogCollapse );

	// now do any dynamic lighting needed
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 0 )
#endif
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE && !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		if ( !fogCollapse ) {
#ifdef USE_VULKAN
			rebindIndex = ProjectDlightTexture();
#else	
			ProjectDlightTexture();
#endif
		}
	}
#endif // USE_LEGACY_DLIGHTS

	// now do fog
	if ( tess.fogNum && tess.shader->fogPass && !fogCollapse ) {
#ifdef USE_VULKAN
		RB_FogPass( rebindIndex );
#else
		RB_FogPass();
#endif
	}
}

#else

/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	const shaderCommands_t *input;
	shader_t		*shader;

	RB_DeformTessGeometry();

	input = &tess;
	shader = input->shader;

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	// set polygon offset if necessary
	if ( shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	//
	// if there is only a single pass then we can enable color
	// and texture arrays before we compile, otherwise we need
	// to avoid compiling those arrays since they will change
	// during multipass rendering
	//
	if ( tess.numPasses > 1 )
	{
		setArraysOnce = qfalse;

		GL_ClientState( 1, CLS_NONE );
		GL_ClientState( 0, CLS_NONE );
	}
	else
	{
		// FIXME: we can't do that if going to lighting/fog later?
		setArraysOnce = qtrue;

		GL_ClientState( 0, CLS_COLOR_ARRAY | CLS_TEXCOORD_ARRAY );

		if ( tess.xstages[0] )
		{
			R_ComputeColors( 0, tess.svars.colors, tess.xstages[0] );
			qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors[0].rgba );
			R_ComputeTexCoords( 0, &tess.xstages[0]->bundle[0] );
			qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoordPtr[0] );
			if ( shader->multitextureEnv )
			{
				GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
				R_ComputeTexCoords( 1, &tess.xstages[0]->bundle[1] );
				qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoordPtr[1] );
			}
			else
			{
				GL_ClientState( 1, CLS_NONE );
			}
		}
	}

	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz ); // padded for SIMD

	//
	// lock XYZ
	//
	if ( qglLockArraysEXT )
	{
		qglLockArraysEXT( 0, input->numVertexes );
	}

	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE && !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) )
	{
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass )
	{
		RB_FogPass();
	}

	//
	// unlock arrays
	//
	if ( qglUnlockArraysEXT )
	{
		qglUnlockArraysEXT();
	}

	GL_ClientState( 1, CLS_NONE );

	//
	// reset polygon offset
	//
	if ( shader->polygonOffset )
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}
#endif // !USE_VULKAN


/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	const shaderCommands_t *input;

	input = &tess;

	if ( input->numIndexes == 0 ) {
		//VBO_UnBind();
		return;
	}

	if ( input->numIndexes > SHADER_MAX_INDEXES ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit" );
	}

	if ( input->numVertexes > SHADER_MAX_VERTEXES ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit" );
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort && !backEnd.doneSurfaces ) {
#ifdef USE_VBO
		tess.vboIndex = 0; //VBO_UnBind();
#endif
		return;
	}

	//
	// update performance counters
	//
#ifdef USE_PMLIGHT
	if ( tess.dlightPass ) {
		backEnd.pc.c_lit_batches++;
		backEnd.pc.c_lit_vertices += tess.numVertexes;
		backEnd.pc.c_lit_indices += tess.numIndexes;
	} else
#endif
	{
		backEnd.pc.c_shaders++;
		backEnd.pc.c_vertexes += tess.numVertexes;
		backEnd.pc.c_indexes += tess.numIndexes;
	}
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	//
	// call off to shader specific tess end function
	//
	tess.shader->optimalStageIteratorFunc();

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer ) {
		DrawTris( input );
	}
	if ( r_shownormals->integer ) {
		DrawNormals( input );
	}

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
	tess.numVertexes = 0;

#ifdef USE_VK_PBR
	tess.vbo_model = NULL;
	tess.ibo_model = NULL;
	//VBO_ClearQueue();
#endif

#ifdef USE_VBO
	tess.vboIndex = 0;
	//VBO_ClearQueue();
#endif
}
