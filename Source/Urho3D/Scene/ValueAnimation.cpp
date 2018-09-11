//
// Copyright (c) 2008-2017 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/StringUtils.h"
#include "../IO/Deserializer.h"
#include "../IO/Log.h"
#include "../IO/Serializer.h"
#include "../Resource/XMLFile.h"
#include "../Resource/JSONFile.h"
#include "../Scene/Animatable.h"
#include "../Scene/ObjectAnimation.h"
#include "../Scene/ValueAnimation.h"
#include "../Scene/Tween.h"

#include "../DebugNew.h"

namespace Urho3D
{

const char* interpMethodNames[] =
{
    "None",
    "Linear",
    "Spline",

    "QuadraticIn",
    "QuadraticOut",
    "QuadraticInOut",

    "CubicIn",
    "CubicOut",
    "CubicInOut",

    "QuarticEaseIn",
    "QuarticEaseOut",
    "QuarticEaseInOut",

    "QuinticIn",
    "QuinticOut",
    "QuinticInOut",

    "SineIn",
    "SineOut",
    "SineInOut",

    "CircularIn",
    "CircularOut",
    "CircularInOut",

    "ExponentialIn",
    "ExponentialOut",
    "ExponentialInOut",

    "ElasticIn",
    "ElasticOut",
    "ElasticInOut",

    "BackIn",
    "BackOut",
    "BackInOut",

    "BounceIn",
    "BounceOut",
    "BounceInOut",
    nullptr
};


ValueAnimation::ValueAnimation(Context* context) :
    Resource(context),
    owner_(nullptr),
    interpolationMethod_(IM_LINEAR),
    splineTension_(0.5f),
    valueType_(VAR_NONE),
    interpolatable_(false),
    beginTime_(M_INFINITY),
    endTime_(-M_INFINITY),
    splineTangentsDirty_(false)
{
    phantomKey_.time_ = -NAN;
    phantomKey_.value_ = Variant();
}

ValueAnimation::~ValueAnimation()
{
}

void ValueAnimation::RegisterObject(Context* context)
{
    context->RegisterFactory<ValueAnimation>();
}

bool ValueAnimation::BeginLoad(Deserializer& source)
{
    XMLFile xmlFile(context_);
    if (!xmlFile.Load(source))
        return false;

    return LoadXML(xmlFile.GetRoot());
}

bool ValueAnimation::Save(Serializer& dest) const
{
    XMLFile xmlFile(context_);

    XMLElement rootElem = xmlFile.CreateRoot("valueanimation");
    if (!SaveXML(rootElem))
        return false;

    return xmlFile.Save(dest);
}

bool ValueAnimation::LoadXML(const XMLElement& source)
{
    valueType_ = VAR_NONE;
    eventFrames_.Clear();

    String interpMethodString = source.GetAttribute("interpolationmethod");
    InterpMethod method = (InterpMethod)GetStringListIndex(interpMethodString.CString(), interpMethodNames, IM_LINEAR);

    SetInterpolationMethod(method);
    if (interpolationMethod_ == IM_SPLINE)
        splineTension_ = source.GetFloat("splinetension");

    XMLElement keyFrameElem = source.GetChild("keyframe");
    while (keyFrameElem)
    {
        float time = keyFrameElem.GetFloat("time");
        Variant value = keyFrameElem.GetVariant();
        SetKeyFrame(time, value);

        keyFrameElem = keyFrameElem.GetNext("keyframe");
    }

    XMLElement eventFrameElem = source.GetChild("eventframe");
    while (eventFrameElem)
    {
        float time = eventFrameElem.GetFloat("time");
        unsigned eventType = eventFrameElem.GetUInt("eventtype");
        VariantMap eventData = eventFrameElem.GetChild("eventdata").GetVariantMap();

        SetEventFrame(time, StringHash(eventType), eventData);
        eventFrameElem = eventFrameElem.GetNext("eventframe");
    }

    return true;
}

bool ValueAnimation::SaveXML(XMLElement& dest) const
{
    dest.SetAttribute("interpolationmethod", interpMethodNames[interpolationMethod_]);
    if (interpolationMethod_ == IM_SPLINE)
        dest.SetFloat("splinetension", splineTension_);

    for (unsigned i = 0; i < keyFrames_.Size(); ++i)
    {
        const VAnimKeyFrame& keyFrame = keyFrames_[i];
        XMLElement keyFrameEleme = dest.CreateChild("keyframe");
        keyFrameEleme.SetFloat("time", keyFrame.time_);
        keyFrameEleme.SetVariant(keyFrame.value_);
    }

    for (unsigned i = 0; i < eventFrames_.Size(); ++i)
    {
        const VAnimEventFrame& eventFrame = eventFrames_[i];
        XMLElement eventFrameElem = dest.CreateChild("eventframe");
        eventFrameElem.SetFloat("time", eventFrame.time_);
        eventFrameElem.SetUInt("eventtype", eventFrame.eventType_.Value());
        eventFrameElem.CreateChild("eventdata").SetVariantMap(eventFrame.eventData_);
    }

    return true;
}

bool ValueAnimation::LoadJSON(const JSONValue& source)
{
    valueType_ = VAR_NONE;
    eventFrames_.Clear();

    String interpMethodString = source.Get("interpolationmethod").GetString();
    InterpMethod method = (InterpMethod)GetStringListIndex(interpMethodString.CString(), interpMethodNames, IM_LINEAR);

    SetInterpolationMethod(method);
    if (interpolationMethod_ == IM_SPLINE)
        splineTension_ = source.Get("splinetension").GetFloat();

    // Load keyframes
    JSONArray keyFramesArray = source.Get("keyframes").GetArray();
    for (unsigned i = 0; i < keyFramesArray.Size(); i++)
    {
        const JSONValue& val = keyFramesArray[i];
        float time = val.Get("time").GetFloat();
        Variant value = val.Get("value").GetVariant();
        SetKeyFrame(time, value);
    }

    // Load event frames
    JSONArray eventFramesArray = source.Get("eventframes").GetArray();
    for (unsigned i = 0; i < eventFramesArray.Size(); i++)
    {
        const JSONValue& eventFrameVal = eventFramesArray[i];
        float time = eventFrameVal.Get("time").GetFloat();
        unsigned eventType = eventFrameVal.Get("eventtype").GetUInt();
        VariantMap eventData = eventFrameVal.Get("eventdata").GetVariantMap();
        SetEventFrame(time, StringHash(eventType), eventData);
    }

    return true;
}

bool ValueAnimation::SaveJSON(JSONValue& dest) const
{
    dest.Set("interpolationmethod", interpMethodNames[interpolationMethod_]);
    if (interpolationMethod_ == IM_SPLINE)
        dest.Set("splinetension", (float) splineTension_);

    JSONArray keyFramesArray;
    keyFramesArray.Reserve(keyFrames_.Size());
    for (unsigned i = 0; i < keyFrames_.Size(); ++i)
    {
        const VAnimKeyFrame& keyFrame = keyFrames_[i];
        JSONValue keyFrameVal;
        keyFrameVal.Set("time", keyFrame.time_);
        JSONValue valueVal;
        valueVal.SetVariant(keyFrame.value_);
        keyFrameVal.Set("value", valueVal);
        keyFramesArray.Push(keyFrameVal);
    }
    dest.Set("keyframes", keyFramesArray);

    JSONArray eventFramesArray;
    eventFramesArray.Reserve(eventFrames_.Size());
    for (unsigned i = 0; i < eventFrames_.Size(); ++i)
    {
        const VAnimEventFrame& eventFrame = eventFrames_[i];
        JSONValue eventFrameVal;
        eventFrameVal.Set("time", eventFrame.time_);
        eventFrameVal.Set("eventtype", eventFrame.eventType_.Value());
        JSONValue eventDataVal;
        eventDataVal.SetVariantMap(eventFrame.eventData_);
        eventFrameVal.Set("eventdata", eventDataVal);

        eventFramesArray.Push(eventFrameVal);
    }
    dest.Set("eventframes", eventFramesArray);

    return true;
}

void ValueAnimation::SetValueType(VariantType valueType)
{
    if (valueType == valueType_)
        return;

    valueType_ = valueType;
    interpolatable_ =
        (valueType_ == VAR_FLOAT) || (valueType_ == VAR_VECTOR2) || (valueType_ == VAR_VECTOR3) || (valueType_ == VAR_VECTOR4) ||
        (valueType_ == VAR_QUATERNION) || (valueType_ == VAR_COLOR);

    if ((valueType_ == VAR_INTRECT) || (valueType_ == VAR_INTVECTOR2) || (valueType_ == VAR_INTVECTOR3))
    {
        interpolatable_ = true;
        // Force linear interpolation for IntRect, IntVector2 and IntVector3
        if (interpolationMethod_ == IM_SPLINE)
            interpolationMethod_ = IM_LINEAR;
    }

    keyFrames_.Clear();
    eventFrames_.Clear();
    beginTime_ = M_INFINITY;
    endTime_ = -M_INFINITY;
}

void ValueAnimation::ClearKeyFrames()
{
    keyFrames_.Clear();
    beginTime_ = M_INFINITY;
    endTime_ = -M_INFINITY;
}

void ValueAnimation::SetOwner(void* owner)
{
    owner_ = owner;
}

void ValueAnimation::SetInterpolationMethod(InterpMethod method)
{
    if (method == interpolationMethod_)
        return;

    // Force linear interpolation for IntRect, IntVector2 and IntVector3
    if (method == IM_SPLINE && (valueType_ == VAR_INTRECT || valueType_ == VAR_INTVECTOR2 || valueType_ == VAR_INTVECTOR3))
        method = IM_LINEAR;

    interpolationMethod_ = method;
    tween_.Reset();

    switch( interpolationMethod_ )
    {
        case IM_QUAD_IN:       tween_ = new QuadraticEaseIn(); break;
        case IM_QUAD_OUT:      tween_ = new QuadraticEaseOut(); break;
        case IM_QUAD_INOUT:    tween_ = new QuadraticEaseInOut(); break;

        case IM_CUBIC_IN:      tween_ = new CubicEaseIn(); break;
        case IM_CUBIC_OUT:     tween_ = new CubicEaseOut(); break;
        case IM_CUBIC_INOUT:   tween_ = new CubicEaseInOut(); break;

        case IM_QUART_IN:      tween_ = new QuarticEaseIn(); break;
        case IM_QUART_OUT:     tween_ = new QuarticEaseOut(); break;
        case IM_QUART_INOUT:   tween_ = new QuarticEaseInOut(); break;

        case IM_QUINT_IN:      tween_ = new QuinticEaseIn(); break;
        case IM_QUINT_OUT:     tween_ = new QuinticEaseOut(); break;
        case IM_QUINT_INOUT:   tween_ = new QuinticEaseInOut(); break;

        case IM_CIRCULAR_IN:   tween_ = new CircularEaseIn(); break;
        case IM_CIRCULAR_OUT:  tween_ = new CircularEaseOut(); break;
        case IM_CIRCULAR_INOUT: tween_ = new CircularEaseInOut(); break;

        case IM_SINE_IN:        tween_ = new SineEaseIn(); break;
        case IM_SINE_OUT:       tween_ = new SineEaseOut(); break;
        case IM_SINE_INOUT:     tween_ = new SineEaseInOut(); break;

        case IM_EXPO_IN:       tween_ = new ExponentialEaseIn(); break;
        case IM_EXPO_OUT:      tween_ = new ExponentialEaseOut(); break;
        case IM_EXPO_INOUT:    tween_ = new ExponentialEaseInOut(); break;

        case IM_ELASTIC_IN:    tween_ = new ElasticEaseIn(); break;
        case IM_ELASTIC_OUT:   tween_ = new ElasticEaseOut(); break;
        case IM_ELASTIC_INOUT: tween_ = new ElasticEaseInOut(); break;

        case IM_BACK_IN:       tween_ = new BackEaseIn(); break;
        case IM_BACK_OUT:      tween_ = new BackEaseOut(); break;
        case IM_BACK_INOUT:    tween_ = new BackEaseInOut(); break;

        case IM_BOUNCE_IN:     tween_ = new BounceEaseIn(); break;
        case IM_BOUNCE_OUT:    tween_ = new BounceEaseOut(); break;
        case IM_BOUNCE_INOUT:  tween_ = new BounceEaseInOut(); break;

    default:
        break;
    }

    splineTangentsDirty_ = true;
}

void ValueAnimation::SetSplineTension(float tension)
{
    splineTension_ = tension;
    splineTangentsDirty_ = true;
}

int ValueAnimation::MoveKeyFrame( int id, float time )
{
    // avoid replace other
    for (unsigned i = 0; i < keyFrames_.Size(); ++i)
         if ( i != id && time == keyFrames_[i].time_)
            return id;

    VAnimKeyFrame deletedKeyFrame = DeleteKeyFrame( id );
    if ( deletedKeyFrame.time_ != -NAN )
        if( SetKeyFrame( time, deletedKeyFrame.value_ ) )
            for (unsigned i = 0; i < keyFrames_.Size(); ++i)
                 if ( time == keyFrames_[i].time_)
                    return i;

    return id;
}

bool ValueAnimation::SetKeyFrame(float time, const Variant& value, bool phantom)
{
    if (valueType_ == VAR_NONE)
        SetValueType(value.GetType());
    else if (value.GetType() != valueType_ && value.GetType() != VAR_NONE ) // phantom key update case
        return false;

    VAnimKeyFrame keyFrame;
    keyFrame.time_ = time;
    keyFrame.value_ = !phantom ? value : Variant();

    if (keyFrames_.Empty() || time > keyFrames_.Back().time_)
        keyFrames_.Push(keyFrame);
    else
    {
        for (unsigned i = 0; i < keyFrames_.Size(); ++i)
        {
            if (time == keyFrames_[i].time_)
            {
                keyFrames_.At(i).value_ = value;
                break;
            }
            else if (time < keyFrames_[i].time_)
            {
                keyFrames_.Insert(i, keyFrame);
                break;
            }
        }
    }

    //beginTime_ = Min( time, beginTime_ );
    //endTime_ = Max( time, endTime_ );
    beginTime_ = keyFrames_.Front().time_;
    endTime_ = keyFrames_.Back().time_;
    splineTangentsDirty_ = true;

    return true;
}

VAnimKeyFrame ValueAnimation::DeleteKeyFrame( int id )
{
    VAnimKeyFrame keyframe;
    keyframe.time_ = -NAN;

    if ( !keyFrames_.Empty() )
    {
        if ( id < keyFrames_.Size() )
        {
            keyframe = keyFrames_.At(id);
            keyFrames_.Erase( id );
            if ( keyFrames_.Size() )
            {
                beginTime_ = keyFrames_.Front().time_;
                endTime_ = keyFrames_.Back().time_;
            }
            splineTangentsDirty_ = true;
        }
    }

    // return deleted keyframe copy
    return keyframe;
}

void ValueAnimation::SetEventFrame(float time, const StringHash& eventType, const VariantMap& eventData)
{
    VAnimEventFrame eventFrame;
    eventFrame.time_ = time;
    eventFrame.eventType_ = eventType;
    eventFrame.eventData_ = eventData;

    if (eventFrames_.Empty() || time >= eventFrames_.Back().time_)
        eventFrames_.Push(eventFrame);
    else
    {
        for (unsigned i = 0; i < eventFrames_.Size(); ++i)
        {
            if (time < eventFrames_[i].time_)
            {
                eventFrames_.Insert(i, eventFrame);
                break;
            }
        }
    }

    beginTime_ = Min(time, beginTime_);
    endTime_ = Max(time, endTime_);
}

bool ValueAnimation::IsValid() const
{
    return (interpolationMethod_ == IM_NONE) ||
           (interpolationMethod_ == IM_SPLINE && keyFrames_.Size() > 2 ) ||
           (interpolationMethod_ != IM_NONE && interpolationMethod_ != IM_SPLINE && keyFrames_.Size() > 1);
}

Variant ValueAnimation::GetAnimationValue(float scaledTime, const Variant &currentValue )
{
    unsigned index = 1;
    for (; index < keyFrames_.Size(); ++index)
    {
        if (scaledTime < keyFrames_[index].time_)
        {
            const VAnimKeyFrame& prev = keyFrames_[index-1];
            if( prev.value_.IsEmpty() && phantomKey_.time_ != prev.time_ )
            {
                phantomKey_ = keyFrames_[index-1];
                phantomKey_.value_ = currentValue;
            }
            else if( !prev.value_.IsEmpty() )
            {
                // reset phantom
                phantomKey_.time_ = -NAN;
                phantomKey_.value_ = Variant();
            }
            break;
        }
        else if( index == keyFrames_.Size() - 1 )
        {
            // reset phantom
            phantomKey_.time_ = -NAN;
            phantomKey_.value_ = Variant();
        }
    }

    if (index >= keyFrames_.Size() || !interpolatable_ || interpolationMethod_ == IM_NONE)
        return keyFrames_[index - 1].value_;
    else
    {
        if (interpolationMethod_ == IM_SPLINE)
            return SplineInterpolation(index - 1, index, scaledTime, currentValue);
        else
            return LinearInterpolation(index - 1, index, scaledTime, currentValue);
    }
}

void ValueAnimation::GetEventFrames(float beginTime, float endTime, PODVector<const VAnimEventFrame*>& eventFrames) const
{
    for (unsigned i = 0; i < eventFrames_.Size(); ++i)
    {
        const VAnimEventFrame& eventFrame = eventFrames_[i];
        if (eventFrame.time_ > endTime)
            break;

        if (eventFrame.time_ >= beginTime)
            eventFrames.Push(&eventFrame);
    }
}

Variant ValueAnimation::LinearInterpolation(unsigned index1, unsigned index2, float scaledTime, const Variant &currentValue) const
{
    const VAnimKeyFrame& keyFrame1 = keyFrames_[index1];
    const VAnimKeyFrame& keyFrame2 = keyFrames_[index2];

    float t = (scaledTime - keyFrame1.time_) / (keyFrame2.time_ - keyFrame1.time_);

    // TWEEN ADDON BASED ON LINEAR
    if( interpolationMethod_ != IM_LINEAR && tween_.Get() )
    {
        t = (*tween_)( t );
    }

    Variant value1 = keyFrame1.value_;
    if( value1.IsEmpty() && !phantomKey_.value_.IsEmpty() )
        value1 = phantomKey_.value_;

    const Variant& value2 = keyFrame2.value_;

    switch (valueType_)
    {
    case VAR_FLOAT:
        return Lerp(value1.GetFloat(), value2.GetFloat(), t);

    case VAR_VECTOR2:
        return value1.GetVector2().Lerp(value2.GetVector2(), t);

    case VAR_VECTOR3:
        return value1.GetVector3().Lerp(value2.GetVector3(), t);

    case VAR_VECTOR4:
        return value1.GetVector4().Lerp(value2.GetVector4(), t);

    case VAR_QUATERNION:
        return value1.GetQuaternion().Slerp(value2.GetQuaternion(), t);

    case VAR_COLOR:
        return value1.GetColor().Lerp(value2.GetColor(), t);

    case VAR_MATRIX3X4:
        {
            const Matrix3x4& m34Value1 = value1.GetMatrix3x4();
            const Matrix3x4& m34Value2 = value2.GetMatrix3x4();

            Vector3 v = m34Value1.Translation().Lerp(m34Value2.Translation(), t);
            Quaternion q = m34Value1.Rotation().Slerp(m34Value2.Rotation(), t);
            Vector3 s = m34Value1.Scale().Lerp(m34Value2.Scale(), t);

            return Matrix3x4( v, q, s );
        }
    case VAR_INTRECT:
        {
            float s = 1.0f - t;
            const IntRect& r1 = value1.GetIntRect();
            const IntRect& r2 = value2.GetIntRect();
            return IntRect((int)(r1.left_ * s + r2.left_ * t), (int)(r1.top_ * s + r2.top_ * t), (int)(r1.right_ * s + r2.right_ * t),
                (int)(r1.bottom_ * s + r2.bottom_ * t));
        }

    case VAR_INTVECTOR2:
        {
            float s = 1.0f - t;
            const IntVector2& v1 = value1.GetIntVector2();
            const IntVector2& v2 = value2.GetIntVector2();
            return IntVector2((int)(v1.x_ * s + v2.x_ * t), (int)(v1.y_ * s + v2.y_ * t));
        }

    case VAR_INTVECTOR3:
        {
            float s = 1.0f - t;
            const IntVector3& v1 = value1.GetIntVector3();
            const IntVector3& v2 = value2.GetIntVector3();
            return IntVector3((int)(v1.x_ * s + v2.x_ * t), (int)(v1.y_ * s + v2.y_ * t), (int)(v1.z_ * s + v2.z_ * t));
        }

    case VAR_DOUBLE:
        return value1.GetDouble() * (1.0f - t) + value2.GetDouble() * t;

    default:
        URHO3D_LOGERROR("Invalid value type for linear interpolation");
        return Variant::EMPTY;
    }
}

Variant ValueAnimation::SplineInterpolation(unsigned index1, unsigned index2, float scaledTime, const Variant &currentValue)
{
    if (splineTangentsDirty_)
        UpdateSplineTangents();

    const VAnimKeyFrame& keyFrame1 = keyFrames_[index1];
    const VAnimKeyFrame& keyFrame2 = keyFrames_[index2];

    float t = (scaledTime - keyFrame1.time_) / (keyFrame2.time_ - keyFrame1.time_);

    float tt = t * t;
    float ttt = t * tt;

    float h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
    float h2 = -2.0f * ttt + 3.0f * tt;
    float h3 = ttt - 2.0f * tt + t;
    float h4 = ttt - tt;

    Variant v1 = keyFrame1.value_;
    if( v1.IsEmpty() && !phantomKey_.value_.IsEmpty() )
        v1 = phantomKey_.value_;

    const Variant& v2 = keyFrame2.value_;
    const Variant& t1 = splineTangents_[index1];
    const Variant& t2 = splineTangents_[index2];

    switch (valueType_)
    {
    case VAR_FLOAT:
        return v1.GetFloat() * h1 + v2.GetFloat() * h2 + t1.GetFloat() * h3 + t2.GetFloat() * h4;

    case VAR_VECTOR2:
        return v1.GetVector2() * h1 + v2.GetVector2() * h2 + t1.GetVector2() * h3 + t2.GetVector2() * h4;

    case VAR_VECTOR3:
        return v1.GetVector3() * h1 + v2.GetVector3() * h2 + t1.GetVector3() * h3 + t2.GetVector3() * h4;

    case VAR_VECTOR4:
        return v1.GetVector4() * h1 + v2.GetVector4() * h2 + t1.GetVector4() * h3 + t2.GetVector4() * h4;

    case VAR_QUATERNION:
        return v1.GetQuaternion() * h1 + v2.GetQuaternion() * h2 + t1.GetQuaternion() * h3 + t2.GetQuaternion() * h4;

    case VAR_COLOR:
        return v1.GetColor() * h1 + v2.GetColor() * h2 + t1.GetColor() * h3 + t2.GetColor() * h4;

    case VAR_DOUBLE:
        return v1.GetDouble() * h1 + v2.GetDouble() * h2 + t1.GetDouble() * h3 + t2.GetDouble() * h4;

    default:
        URHO3D_LOGERROR("Invalid value type for spline interpolation");
        return Variant::EMPTY;
    }
}

void ValueAnimation::UpdateSplineTangents()
{
    splineTangents_.Clear();

    if (!IsValid())
        return;

    unsigned size = keyFrames_.Size();
    splineTangents_.Resize(size);

    for (unsigned i = 1; i < size - 1; ++i)
        splineTangents_[i] = SubstractAndMultiply(keyFrames_[i + 1].value_, keyFrames_[i - 1].value_, splineTension_);

    // If spline is not closed, make end point's tangent zero
    if (keyFrames_[0].value_ != keyFrames_[size - 1].value_)
        splineTangents_[0] = splineTangents_[size - 1] =
            SubstractAndMultiply(keyFrames_[0].value_, keyFrames_[0].value_, splineTension_);
    else
        splineTangents_[0] = splineTangents_[size - 1] =
            SubstractAndMultiply(keyFrames_[1].value_, keyFrames_[size - 2].value_, splineTension_);

    splineTangentsDirty_ = false;
}

Variant ValueAnimation::SubstractAndMultiply(const Variant& value1, const Variant& value2, float t) const
{
    switch (valueType_)
    {
    case VAR_FLOAT:
        return (value1.GetFloat() - value2.GetFloat()) * t;

    case VAR_VECTOR2:
        return (value1.GetVector2() - value2.GetVector2()) * t;

    case VAR_VECTOR3:
        return (value1.GetVector3() - value2.GetVector3()) * t;

    case VAR_VECTOR4:
        return (value1.GetVector4() - value2.GetVector4()) * t;

    case VAR_QUATERNION:
        return (value1.GetQuaternion() - value2.GetQuaternion()) * t;

    case VAR_COLOR:
        return (value1.GetColor() - value2.GetColor()) * t;

    case VAR_DOUBLE:
        return (value1.GetDouble() - value2.GetDouble()) * t;

    default:
        URHO3D_LOGERROR("Invalid value type for spline interpolation's subtract and multiply operation");
        return Variant::EMPTY;
    }
}

}
