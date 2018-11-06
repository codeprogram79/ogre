/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreShaderPrecompiledHeaders.h"
#ifdef RTSHADER_SYSTEM_BUILD_CORE_SHADERS

namespace Ogre {
namespace RTShader {

/************************************************************************/
/*                                                                      */
/************************************************************************/
String FFPTexturing::Type = "FFP_Texturing";
#define _INT_VALUE(f) (*(int*)(&(f)))

const String c_ParamTexelEx("texel_");

//-----------------------------------------------------------------------
FFPTexturing::FFPTexturing() : mIsPointSprite(false)
{   
}

//-----------------------------------------------------------------------
const String& FFPTexturing::getType() const
{
    return Type;
}

//-----------------------------------------------------------------------
int FFPTexturing::getExecutionOrder() const
{       
    return FFP_TEXTURING;
}

//-----------------------------------------------------------------------
bool FFPTexturing::resolveParameters(ProgramSet* programSet)
{
    for (unsigned int i=0; i < mTextureUnitParamsList.size(); ++i)
    {
        TextureUnitParams* curParams = &mTextureUnitParamsList[i];

        if (false == resolveUniformParams(curParams, programSet))
            return false;


        if (false == resolveFunctionsParams(curParams, programSet))
            return false;
    }
    

    return true;
}

//-----------------------------------------------------------------------
bool FFPTexturing::resolveUniformParams(TextureUnitParams* textureUnitParams, ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    bool hasError = false;
    
    // Resolve texture sampler parameter.       
    textureUnitParams->mTextureSampler = psProgram->resolveParameter(textureUnitParams->mTextureSamplerType, textureUnitParams->mTextureSamplerIndex, (uint16)GPV_GLOBAL, "gTextureSampler");
    hasError |= !textureUnitParams->mTextureSampler;

    // Resolve texture matrix parameter.
    if (needsTextureMatrix(textureUnitParams->mTextureUnitState))
    {               
        textureUnitParams->mTextureMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_TEXTURE_MATRIX, textureUnitParams->mTextureSamplerIndex);
        hasError |= !(textureUnitParams->mTextureMatrix.get());
    }

    switch (textureUnitParams->mTexCoordCalcMethod)
    {
    case TEXCALC_NONE:                              
        break;

    // Resolve World + View matrices.
    case TEXCALC_ENVIRONMENT_MAP:
    case TEXCALC_ENVIRONMENT_MAP_PLANAR:    
    case TEXCALC_ENVIRONMENT_MAP_NORMAL:
        //TODO: change the following 'mWorldITMatrix' member to 'mWorldViewITMatrix'
        mWorldITMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_INVERSE_TRANSPOSE_WORLDVIEW_MATRIX, 0);
        mViewMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_VIEW_MATRIX, 0);
        mWorldMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_WORLD_MATRIX, 0);
        
        hasError |= !(mWorldITMatrix.get())  || !(mViewMatrix.get()) || !(mWorldMatrix.get());
        break;

    case TEXCALC_ENVIRONMENT_MAP_REFLECTION:
        mWorldMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_WORLD_MATRIX, 0);
        mWorldITMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_INVERSE_TRANSPOSE_WORLD_MATRIX, 0);
        mViewMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_VIEW_MATRIX, 0);
        
        hasError |= !(mWorldMatrix.get()) || !(mWorldITMatrix.get()) || !(mViewMatrix.get());
        break;

    case TEXCALC_PROJECTIVE_TEXTURE:

        mWorldMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_WORLD_MATRIX, 0);
        textureUnitParams->mTextureViewProjImageMatrix = vsProgram->resolveParameter(GCT_MATRIX_4X4, -1, (uint16)GPV_LIGHTS, "gTexViewProjImageMatrix");
        
        hasError |= !(mWorldMatrix.get()) || !(textureUnitParams->mTextureViewProjImageMatrix.get());
        
        const TextureUnitState::EffectMap&      effectMap = textureUnitParams->mTextureUnitState->getEffects(); 
        TextureUnitState::EffectMap::const_iterator effi;

        for (effi = effectMap.begin(); effi != effectMap.end(); ++effi)
        {
            if (effi->second.type == TextureUnitState::ET_PROJECTIVE_TEXTURE)
            {
                textureUnitParams->mTextureProjector = effi->second.frustum;
                break;
            }
        }

        hasError |= !(textureUnitParams->mTextureProjector);
        break;
    }

    
    if (hasError)
    {
        OGRE_EXCEPT( Exception::ERR_INTERNAL_ERROR, 
                "Not all parameters could be constructed for the sub-render state.",
                "FFPTexturing::resolveUniformParams" );
    }
    return true;
}



//-----------------------------------------------------------------------
bool FFPTexturing::resolveFunctionsParams(TextureUnitParams* textureUnitParams, ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain   = vsProgram->getEntryPointFunction();
    Function* psMain   = psProgram->getEntryPointFunction();
    Parameter::Content texCoordContent = Parameter::SPC_UNKNOWN;
    bool hasError = false;

    switch (textureUnitParams->mTexCoordCalcMethod)
    {
        case TEXCALC_NONE:                  
            // Resolve explicit vs input texture coordinates.
            
            if(mIsPointSprite)
                break;

            if (textureUnitParams->mTextureMatrix.get() == NULL)
                texCoordContent = Parameter::Content(Parameter::SPC_TEXTURE_COORDINATE0 + textureUnitParams->mTextureUnitState->getTextureCoordSet());

            textureUnitParams->mVSInputTexCoord = vsMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 
                textureUnitParams->mTextureUnitState->getTextureCoordSet(), 
                Parameter::Content(Parameter::SPC_TEXTURE_COORDINATE0 + textureUnitParams->mTextureUnitState->getTextureCoordSet()),
                textureUnitParams->mVSInTextureCoordinateType); 
            hasError |= !(textureUnitParams->mVSInputTexCoord.get());
            break;

        case TEXCALC_ENVIRONMENT_MAP:
        case TEXCALC_ENVIRONMENT_MAP_PLANAR:        
        case TEXCALC_ENVIRONMENT_MAP_NORMAL:
            // Resolve vertex normal.
            mVSInputPos = vsMain->resolveInputParameter(Parameter::SPS_POSITION, 0, Parameter::SPC_POSITION_OBJECT_SPACE, GCT_FLOAT4);
            mVSInputNormal = vsMain->resolveInputParameter(Parameter::SPS_NORMAL, 0, Parameter::SPC_NORMAL_OBJECT_SPACE, GCT_FLOAT3);
            hasError |= !(mVSInputNormal.get()) || !(mVSInputPos.get());
            break;  

        case TEXCALC_ENVIRONMENT_MAP_REFLECTION:

            // Resolve vertex normal.
            mVSInputNormal = vsMain->resolveInputParameter(Parameter::SPS_NORMAL, 0, Parameter::SPC_NORMAL_OBJECT_SPACE, GCT_FLOAT3);
            // Resolve vertex position.
            mVSInputPos = vsMain->resolveInputParameter(Parameter::SPS_POSITION, 0, Parameter::SPC_POSITION_OBJECT_SPACE, GCT_FLOAT4);
            
            hasError |= !(mVSInputNormal.get()) || !(mVSInputPos.get());
            break;

        case TEXCALC_PROJECTIVE_TEXTURE:
            // Resolve vertex position.
            mVSInputPos = vsMain->resolveInputParameter(Parameter::SPS_POSITION, 0, Parameter::SPC_POSITION_OBJECT_SPACE, GCT_FLOAT4);
            hasError |= !(mVSInputPos.get());
            break;
    }

    if(mIsPointSprite)
    {
        textureUnitParams->mPSInputTexCoord =
            psMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 0,
                                          Parameter::SPC_POINTSPRITE_COORDINATE, GCT_FLOAT2);
    }
    else
    {
        // Resolve vs output texture coordinates.
        textureUnitParams->mVSOutputTexCoord = vsMain->resolveOutputParameter(Parameter::SPS_TEXTURE_COORDINATES,
            -1,
            texCoordContent,
            textureUnitParams->mVSOutTextureCoordinateType);

        // Resolve ps input texture coordinates.
        textureUnitParams->mPSInputTexCoord = psMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES,
            textureUnitParams->mVSOutputTexCoord->getIndex(),
            textureUnitParams->mVSOutputTexCoord->getContent(),
            textureUnitParams->mVSOutTextureCoordinateType);
    }

    const ShaderParameterList& inputParams = psMain->getInputParameters();
    const ShaderParameterList& localParams = psMain->getLocalParameters();

    mPSDiffuse = psMain->getParameterByContent(inputParams, Parameter::SPC_COLOR_DIFFUSE, GCT_FLOAT4);
    if (mPSDiffuse.get() == NULL)
    {
        mPSDiffuse = psMain->getParameterByContent(localParams, Parameter::SPC_COLOR_DIFFUSE, GCT_FLOAT4);
    }

    mPSSpecular = psMain->getParameterByContent(inputParams, Parameter::SPC_COLOR_SPECULAR, GCT_FLOAT4);
    if (mPSSpecular.get() == NULL)
    {
        mPSSpecular = psMain->getParameterByContent(localParams, Parameter::SPC_COLOR_SPECULAR, GCT_FLOAT4);
    }

    mPSOutDiffuse = psMain->resolveOutputParameter(Parameter::SPS_COLOR, 0, Parameter::SPC_COLOR_DIFFUSE, GCT_FLOAT4);

    hasError |= (!textureUnitParams->mVSOutputTexCoord && !mIsPointSprite) ||
                !textureUnitParams->mPSInputTexCoord || !mPSDiffuse || !mPSSpecular ||
                !mPSOutDiffuse;

    if (hasError)
    {
        OGRE_EXCEPT( Exception::ERR_INTERNAL_ERROR, 
                "Not all parameters could be constructed for the sub-render state.",
                "FFPTexturing::resolveFunctionsParams" );
    }
    return true;
}

//-----------------------------------------------------------------------
bool FFPTexturing::resolveDependencies(ProgramSet* programSet)
{
    //! [deps_resolve]
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);

    vsProgram->addDependency(FFP_LIB_COMMON);
    vsProgram->addDependency(FFP_LIB_TEXTURING);    
    psProgram->addDependency(FFP_LIB_COMMON);
    psProgram->addDependency(FFP_LIB_TEXTURING);
    //! [deps_resolve]
    return true;
}

//-----------------------------------------------------------------------
bool FFPTexturing::addFunctionInvocations(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain   = vsProgram->getEntryPointFunction();
    Function* psMain   = psProgram->getEntryPointFunction();

    for (unsigned int i=0; i < mTextureUnitParamsList.size(); ++i)
    {
        TextureUnitParams* curParams = &mTextureUnitParamsList[i];

        if (false == addVSFunctionInvocations(curParams, vsMain))
            return false;

        if (false == addPSFunctionInvocations(curParams, psMain))
            return false;
    }

    return true;
}

//-----------------------------------------------------------------------
bool FFPTexturing::addVSFunctionInvocations(TextureUnitParams* textureUnitParams, Function* vsMain)
{
    if(mIsPointSprite)
        return true;
    
    auto stage = vsMain->getStage(FFP_VS_TEXTURING);

    switch (textureUnitParams->mTexCoordCalcMethod)
    {
    case TEXCALC_NONE:
        stage.assign(textureUnitParams->mVSInputTexCoord, textureUnitParams->mVSOutputTexCoord);
        break;
    case TEXCALC_ENVIRONMENT_MAP:
    case TEXCALC_ENVIRONMENT_MAP_PLANAR:
        stage.callFunction(FFP_FUNC_GENERATE_TEXCOORD_ENV_SPHERE,
                           {In(mWorldMatrix), In(mViewMatrix), In(mWorldITMatrix), In(mVSInputPos), In(mVSInputNormal),
                            Out(textureUnitParams->mVSOutputTexCoord)});
        break;
    case TEXCALC_ENVIRONMENT_MAP_REFLECTION:
        stage.callFunction(FFP_FUNC_GENERATE_TEXCOORD_ENV_REFLECT,
                           {In(mWorldMatrix), In(mWorldITMatrix), In(mViewMatrix), In(mVSInputNormal), In(mVSInputPos),
                            Out(textureUnitParams->mVSOutputTexCoord)});
        break;
    case TEXCALC_ENVIRONMENT_MAP_NORMAL:
        stage.callFunction(
            FFP_FUNC_GENERATE_TEXCOORD_ENV_NORMAL,
            {In(mWorldITMatrix), In(mViewMatrix), In(mVSInputNormal), Out(textureUnitParams->mVSOutputTexCoord)});
        break;
    case TEXCALC_PROJECTIVE_TEXTURE:
        stage.callFunction(FFP_FUNC_GENERATE_TEXCOORD_PROJECTION,
                           {In(mWorldMatrix), In(textureUnitParams->mTextureViewProjImageMatrix), In(mVSInputPos),
                            Out(textureUnitParams->mVSOutputTexCoord)});
        break;
    default:
        return false;
    }

    if (textureUnitParams->mTextureMatrix)
    {
        stage.callFunction(FFP_FUNC_TRANSFORM_TEXCOORD, textureUnitParams->mTextureMatrix,
                           textureUnitParams->mVSOutputTexCoord, textureUnitParams->mVSOutputTexCoord);
    }

    return true;
}
//-----------------------------------------------------------------------
bool FFPTexturing::addPSFunctionInvocations(TextureUnitParams* textureUnitParams, Function* psMain)
{
    const LayerBlendModeEx& colourBlend = textureUnitParams->mTextureUnitState->getColourBlendMode();
    const LayerBlendModeEx& alphaBlend  = textureUnitParams->mTextureUnitState->getAlphaBlendMode();
    ParameterPtr source1;
    ParameterPtr source2;
    int groupOrder = FFP_PS_TEXTURING;
    
            
    // Add texture sampling code.
    ParameterPtr texel = psMain->resolveLocalParameter(Parameter::SPS_UNKNOWN, 0, c_ParamTexelEx + StringConverter::toString(textureUnitParams->mTextureSamplerIndex), GCT_FLOAT4);
    addPSSampleTexelInvocation(textureUnitParams, psMain, texel, FFP_PS_SAMPLING);

    // Build colour argument for source1.
    source1 = psMain->resolveLocalParameter(Parameter::SPS_UNKNOWN, 0, "source1", GCT_FLOAT4);
        
    addPSArgumentInvocations(psMain, source1, texel, 
        textureUnitParams->mTextureSamplerIndex,
        colourBlend.source1, colourBlend.colourArg1, 
        colourBlend.alphaArg1, false, groupOrder);

    // Build colour argument for source2.
    source2 = psMain->resolveLocalParameter(Parameter::SPS_UNKNOWN, 0, "source2", GCT_FLOAT4);

    addPSArgumentInvocations(psMain, source2, texel, 
        textureUnitParams->mTextureSamplerIndex,
        colourBlend.source2, colourBlend.colourArg2, 
        colourBlend.alphaArg2, false, groupOrder);

    bool needDifferentAlphaBlend = false;
    if (alphaBlend.operation != colourBlend.operation ||
        alphaBlend.source1 != colourBlend.source1 ||
        alphaBlend.source2 != colourBlend.source2 ||
        colourBlend.source1 == LBS_MANUAL ||
        colourBlend.source2 == LBS_MANUAL ||
        alphaBlend.source1 == LBS_MANUAL ||
        alphaBlend.source2 == LBS_MANUAL)
        needDifferentAlphaBlend = true;

    // Build colours blend
    addPSBlendInvocations(psMain, source1, source2, texel, 
        textureUnitParams->mTextureSamplerIndex,
        colourBlend, groupOrder,
        needDifferentAlphaBlend ? Operand::OPM_XYZ : Operand::OPM_ALL);

    // Case we need different alpha channel code.
    if (needDifferentAlphaBlend)
    {
        // Build alpha argument for source1.
        addPSArgumentInvocations(psMain, source1, texel,
            textureUnitParams->mTextureSamplerIndex, 
            alphaBlend.source1, alphaBlend.colourArg1, 
            alphaBlend.alphaArg1, true, groupOrder);

        // Build alpha argument for source2.
        addPSArgumentInvocations(psMain, source2, texel, 
            textureUnitParams->mTextureSamplerIndex,
            alphaBlend.source2, alphaBlend.colourArg2, 
            alphaBlend.alphaArg2, true, groupOrder);

        // Build alpha blend
        addPSBlendInvocations(psMain, source1, source2, texel,
                              textureUnitParams->mTextureSamplerIndex, alphaBlend, groupOrder,
                              Operand::OPM_W);
    }
    
    

    return true;
}

//-----------------------------------------------------------------------
void FFPTexturing::addPSSampleTexelInvocation(TextureUnitParams* textureUnitParams, Function* psMain, 
                                              const ParameterPtr& texel, int groupOrder)
{
    auto stage = psMain->getStage(groupOrder);

    if (textureUnitParams->mTexCoordCalcMethod != TEXCALC_PROJECTIVE_TEXTURE)
    {
        stage.sampleTexture(textureUnitParams->mTextureSampler, textureUnitParams->mPSInputTexCoord, texel);
        return;
    }

    stage.callFunction(FFP_FUNC_SAMPLE_TEXTURE_PROJ, textureUnitParams->mTextureSampler,
                       textureUnitParams->mPSInputTexCoord, texel);
}

//-----------------------------------------------------------------------
void FFPTexturing::addPSArgumentInvocations(Function* psMain, 
                                             ParameterPtr arg,
                                             ParameterPtr texel,
                                             int samplerIndex,
                                             LayerBlendSource blendSrc,
                                             const ColourValue& colourValue,
                                             Real alphaValue,
                                             bool isAlphaArgument,
                                             const int groupOrder)
{
    ParameterPtr src;
    switch(blendSrc)
    {
    case LBS_CURRENT:
        src = samplerIndex == 0 ? mPSDiffuse : mPSOutDiffuse;
        break;
    case LBS_TEXTURE:
        src = texel;
        break;
    case LBS_DIFFUSE:
        src = mPSDiffuse;
        break;
    case LBS_SPECULAR:
        src = mPSSpecular;
        break;
    case LBS_MANUAL:
        if (isAlphaArgument)
        {
            src = ParameterFactory::createConstParam(Vector4(alphaValue));
        }
        else
        {
            src = ParameterFactory::createConstParam(Vector4((Real)colourValue.r, (Real)colourValue.g,
                                                             (Real)colourValue.b, (Real)colourValue.a));
        }
        break;
    }

    psMain->getStage(groupOrder).assign(src, arg);
}

//-----------------------------------------------------------------------
void FFPTexturing::addPSBlendInvocations(Function* psMain, 
                                          ParameterPtr arg1,
                                          ParameterPtr arg2,
                                          ParameterPtr texel,
                                          int samplerIndex,
                                          const LayerBlendModeEx& blendMode,
                                          const int groupOrder, 
                                          int mask)
{
    auto stage = psMain->getStage(groupOrder);
    switch(blendMode.operation)
    {
    case LBX_SOURCE1:
        stage.assign(In(arg1).mask(mask), Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_SOURCE2:
        stage.assign(In(arg2).mask(mask), Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_MODULATE:
        stage.callFunction(FFP_FUNC_MODULATE, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_MODULATE_X2:
        stage.callFunction(FFP_FUNC_MODULATEX2, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_MODULATE_X4:
        stage.callFunction(FFP_FUNC_MODULATEX4, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_ADD:
        stage.callFunction(FFP_FUNC_ADD, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_ADD_SIGNED:
        stage.callFunction(FFP_FUNC_ADDSIGNED, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_ADD_SMOOTH:
        stage.callFunction(FFP_FUNC_ADDSMOOTH, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_SUBTRACT:
        stage.callFunction(FFP_FUNC_SUBTRACT, In(arg1).mask(mask), In(arg2).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_BLEND_DIFFUSE_ALPHA:
        stage.callFunction(FFP_FUNC_LERP, {In(arg2).mask(mask), In(arg1).mask(mask), In(mPSDiffuse).w(),
                                           Out(mPSOutDiffuse).mask(mask)});
        break;
    case LBX_BLEND_TEXTURE_ALPHA:
        stage.callFunction(FFP_FUNC_LERP, {In(arg2).mask(mask), In(arg1).mask(mask), In(texel).w(),
                                           Out(mPSOutDiffuse).mask(mask)});
        break;
    case LBX_BLEND_CURRENT_ALPHA:
        stage.callFunction(FFP_FUNC_LERP, {In(arg2).mask(mask), In(arg1).mask(mask),
                                           In(samplerIndex == 0 ? mPSDiffuse : mPSOutDiffuse).w(),
                                           Out(mPSOutDiffuse).mask(mask)});
        break;
    case LBX_BLEND_MANUAL:
        stage.callFunction(FFP_FUNC_LERP, {In(arg2).mask(mask), In(arg1).mask(mask),
                                           In(ParameterFactory::createConstParam(blendMode.factor)),
                                           Out(mPSOutDiffuse).mask(mask)});
        break;
    case LBX_DOTPRODUCT:
        stage.callFunction(FFP_FUNC_DOTPRODUCT, In(arg2).mask(mask), In(arg1).mask(mask),
                           Out(mPSOutDiffuse).mask(mask));
        break;
    case LBX_BLEND_DIFFUSE_COLOUR:
        stage.callFunction(FFP_FUNC_LERP, {In(arg2).mask(mask), In(arg1).mask(mask),
                                           In(mPSDiffuse).mask(mask), Out(mPSOutDiffuse).mask(mask)});
        break;
    }
}

//-----------------------------------------------------------------------
TexCoordCalcMethod FFPTexturing::getTexCalcMethod(TextureUnitState* textureUnitState)
{
    TexCoordCalcMethod                      texCoordCalcMethod = TEXCALC_NONE;  
    const TextureUnitState::EffectMap&      effectMap = textureUnitState->getEffects(); 
    TextureUnitState::EffectMap::const_iterator effi;
    
    for (effi = effectMap.begin(); effi != effectMap.end(); ++effi)
    {
        switch (effi->second.type)
        {
        case TextureUnitState::ET_ENVIRONMENT_MAP:
            if (effi->second.subtype == TextureUnitState::ENV_CURVED)
            {
                texCoordCalcMethod = TEXCALC_ENVIRONMENT_MAP;               
            }
            else if (effi->second.subtype == TextureUnitState::ENV_PLANAR)
            {
                texCoordCalcMethod = TEXCALC_ENVIRONMENT_MAP_PLANAR;                
            }
            else if (effi->second.subtype == TextureUnitState::ENV_REFLECTION)
            {
                texCoordCalcMethod = TEXCALC_ENVIRONMENT_MAP_REFLECTION;                
            }
            else if (effi->second.subtype == TextureUnitState::ENV_NORMAL)
            {
                texCoordCalcMethod = TEXCALC_ENVIRONMENT_MAP_NORMAL;                
            }
            break;
        case TextureUnitState::ET_UVSCROLL:
        case TextureUnitState::ET_USCROLL:
        case TextureUnitState::ET_VSCROLL:
        case TextureUnitState::ET_ROTATE:
        case TextureUnitState::ET_TRANSFORM:
            break;
        case TextureUnitState::ET_PROJECTIVE_TEXTURE:
            texCoordCalcMethod = TEXCALC_PROJECTIVE_TEXTURE;
            break;
        }
    }

    return texCoordCalcMethod;
}

//-----------------------------------------------------------------------
bool FFPTexturing::needsTextureMatrix(TextureUnitState* textureUnitState)
{
    const TextureUnitState::EffectMap&      effectMap = textureUnitState->getEffects(); 
    TextureUnitState::EffectMap::const_iterator effi;

    for (effi = effectMap.begin(); effi != effectMap.end(); ++effi)
    {
        switch (effi->second.type)
        {
    
        case TextureUnitState::ET_UVSCROLL:
        case TextureUnitState::ET_USCROLL:
        case TextureUnitState::ET_VSCROLL:
        case TextureUnitState::ET_ROTATE:
        case TextureUnitState::ET_TRANSFORM:
        case TextureUnitState::ET_ENVIRONMENT_MAP:
        case TextureUnitState::ET_PROJECTIVE_TEXTURE:
            return true;        
        }
    }

    const Ogre::Matrix4 matTexture = textureUnitState->getTextureTransform();

    // Resolve texture matrix parameter.
    if (matTexture != Matrix4::IDENTITY)
        return true;

    return false;
}


//-----------------------------------------------------------------------
void FFPTexturing::copyFrom(const SubRenderState& rhs)
{
    const FFPTexturing& rhsTexture = static_cast<const FFPTexturing&>(rhs);

    setTextureUnitCount(rhsTexture.getTextureUnitCount());

    for (unsigned int i=0; i < rhsTexture.getTextureUnitCount(); ++i)
    {
        setTextureUnit(i, rhsTexture.mTextureUnitParamsList[i].mTextureUnitState);
    }       
}

//-----------------------------------------------------------------------
bool FFPTexturing::preAddToRenderState(const RenderState* renderState, Pass* srcPass, Pass* dstPass)
{
    mIsPointSprite = srcPass->getPointSpritesEnabled();

    //count the number of texture units we need to process
    size_t validTexUnits = 0;
    for (unsigned short i=0; i < srcPass->getNumTextureUnitStates(); ++i)
    {       
        if (isProcessingNeeded(srcPass->getTextureUnitState(i)))
        {
            ++validTexUnits;
        }
    }

    setTextureUnitCount(validTexUnits);

    // Build texture stage sub states.
    for (unsigned short i=0; i < srcPass->getNumTextureUnitStates(); ++i)
    {       
        TextureUnitState* texUnitState = srcPass->getTextureUnitState(i);                               

        if (isProcessingNeeded(texUnitState))
        {
            setTextureUnit(i, texUnitState);    
        }
    }   

    return true;
}

//-----------------------------------------------------------------------
void FFPTexturing::updateGpuProgramsParams(Renderable* rend, Pass* pass, const AutoParamDataSource* source, 
                                              const LightList* pLightList)
{
    for (unsigned int i=0; i < mTextureUnitParamsList.size(); ++i)
    {
        TextureUnitParams* curParams = &mTextureUnitParamsList[i];

        if (curParams->mTextureProjector != NULL && curParams->mTextureViewProjImageMatrix.get() != NULL)
        {                   
            Matrix4 matTexViewProjImage;

            matTexViewProjImage = 
                Matrix4::CLIPSPACE2DTOIMAGESPACE * 
                curParams->mTextureProjector->getProjectionMatrixWithRSDepth() * 
                curParams->mTextureProjector->getViewMatrix();

            curParams->mTextureViewProjImageMatrix->setGpuParameter(matTexViewProjImage);
        }
    }
}

//-----------------------------------------------------------------------
void FFPTexturing::setTextureUnitCount(size_t count)
{
    mTextureUnitParamsList.resize(count);

    for (unsigned int i=0; i < count; ++i)
    {
        TextureUnitParams& curParams = mTextureUnitParamsList[i];

        curParams.mTextureUnitState             = NULL;         
        curParams.mTextureProjector             = NULL;               
        curParams.mTextureSamplerIndex          = 0;              
        curParams.mTextureSamplerType           = GCT_SAMPLER2D;        
        curParams.mVSInTextureCoordinateType    = GCT_FLOAT2;   
        curParams.mVSOutTextureCoordinateType   = GCT_FLOAT2;       
    }
}

//-----------------------------------------------------------------------
void FFPTexturing::setTextureUnit(unsigned short index, TextureUnitState* textureUnitState)
{
    if (index >= mTextureUnitParamsList.size())
    {
        OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "FFPTexturing unit index out of bounds !!!",
            "FFPTexturing::setTextureUnit");
    }

    if (textureUnitState->getBindingType() == TextureUnitState::BT_VERTEX)
    {
        OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "FFP Texture unit does not support vertex texture fetch !!!",
            "FFPTexturing::setTextureUnit");
    }
    
    if (textureUnitState->getBindingType() == TextureUnitState::BT_GEOMETRY)
    {
        OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "FFP Texture unit does not support geometry texture fetch !!!",
            "FFPTexturing::setTextureUnit");
    }

    if (textureUnitState->getBindingType() == TextureUnitState::BT_COMPUTE)
    {
        OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "FFP Texture unit does not support comput texture fetch !!!",
            "FFPTexturing::setTextureUnit");
    }

    if (textureUnitState->getBindingType() == TextureUnitState::BT_TESSELLATION_DOMAIN)
    {
        OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "FFP Texture unit does not support domain texture fetch !!!",
            "FFPTexturing::setTextureUnit");
    }

    if (textureUnitState->getBindingType() == TextureUnitState::BT_TESSELLATION_HULL)
    {
        OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "FFP Texture unit does not support hull texture fetch !!!",
            "FFPTexturing::setTextureUnit");
    }

    TextureUnitParams& curParams = mTextureUnitParamsList[index];


    curParams.mTextureSamplerIndex = index;
    curParams.mTextureUnitState    = textureUnitState;

    bool isGLES2 = Root::getSingletonPtr()->getRenderSystem()->getName().find("OpenGL ES 2") != String::npos;

    switch (curParams.mTextureUnitState->getTextureType())
    {
    case TEX_TYPE_1D:
        curParams.mTextureSamplerType = GCT_SAMPLER1D;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT1;
        if(!isGLES2) // no 1D texture support
            break;
        OGRE_FALLTHROUGH;
    case TEX_TYPE_2D:
        curParams.mTextureSamplerType = GCT_SAMPLER2D;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT2;
        break;
    case TEX_TYPE_2D_RECT:
        curParams.mTextureSamplerType = GCT_SAMPLERRECT;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT2;
        break;
    case TEX_TYPE_EXTERNAL_OES:
        curParams.mTextureSamplerType = GCT_SAMPLER_EXTERNAL_OES;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT2;
        break;
    case TEX_TYPE_2D_ARRAY:
        curParams.mTextureSamplerType = GCT_SAMPLER2DARRAY;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT3;
        break;
    case TEX_TYPE_3D:
        curParams.mTextureSamplerType = GCT_SAMPLER3D;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT3;
        break;
    case TEX_TYPE_CUBE_MAP:
        curParams.mTextureSamplerType = GCT_SAMPLERCUBE;
        curParams.mVSInTextureCoordinateType = GCT_FLOAT3;
        break;
    }   

     curParams.mVSOutTextureCoordinateType = curParams.mVSInTextureCoordinateType;
     curParams.mTexCoordCalcMethod = getTexCalcMethod(curParams.mTextureUnitState);

     if (curParams.mTexCoordCalcMethod == TEXCALC_PROJECTIVE_TEXTURE)
         curParams.mVSOutTextureCoordinateType = GCT_FLOAT3;    
}

//-----------------------------------------------------------------------
bool FFPTexturing::isProcessingNeeded(TextureUnitState* texUnitState)
{
    return texUnitState->getBindingType() == TextureUnitState::BT_FRAGMENT;
}


//-----------------------------------------------------------------------
const String& FFPTexturingFactory::getType() const
{
    return FFPTexturing::Type;
}

//-----------------------------------------------------------------------
SubRenderState* FFPTexturingFactory::createInstance(ScriptCompiler* compiler, 
                                                 PropertyAbstractNode* prop, Pass* pass, SGScriptTranslator* translator)
{
    if (prop->name == "texturing_stage")
    {
        if(prop->values.size() == 1)
        {
            String modelType;

            if(false == SGScriptTranslator::getString(prop->values.front(), &modelType))
            {
                compiler->addError(ScriptCompiler::CE_INVALIDPARAMETERS, prop->file, prop->line);
                return NULL;
            }

            if (modelType == "ffp")
            {
                return createOrRetrieveInstance(translator);
            }
        }       
    }

    return NULL;
}

//-----------------------------------------------------------------------
void FFPTexturingFactory::writeInstance(MaterialSerializer* ser, SubRenderState* subRenderState, 
                                     Pass* srcPass, Pass* dstPass)
{
    ser->writeAttribute(4, "texturing_stage");
    ser->writeValue("ffp");
}

//-----------------------------------------------------------------------
SubRenderState* FFPTexturingFactory::createInstanceImpl()
{
    return OGRE_NEW FFPTexturing;
}


}
}

#endif
