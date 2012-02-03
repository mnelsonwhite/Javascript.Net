////////////////////////////////////////////////////////////////////////////////////////////////////
// File: JavascriptInterop.cpp
// 
// Copyright 2010 Noesis Innovation Inc. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "JavascriptInterop.h"

#include "SystemInterop.h"
#include "JavascriptContext.h"
#include "JavascriptException.h"
#include "JavascriptExternal.h"

#include <string>

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Noesis { namespace Javascript {

////////////////////////////////////////////////////////////////////////////////////////////////////

using namespace std;
using namespace System::Collections;
using namespace System::Collections::Generic;

////////////////////////////////////////////////////////////////////////////////////////////////////

Persistent<ObjectTemplate> JavascriptInterop::sObjectWrapperTemplate;

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<ObjectTemplate>
JavascriptInterop::GetObjectWrapperTemplate()
{
	if (sObjectWrapperTemplate.IsEmpty())
	{
		HandleScope handleScope;

		Handle<ObjectTemplate> result = ObjectTemplate::New();
		result->SetInternalFieldCount(1);
		result->SetNamedPropertyHandler(Getter, Setter);
		result->SetIndexedPropertyHandler(IndexGetter, IndexSetter);
		sObjectWrapperTemplate = Persistent<ObjectTemplate>::New(handleScope.Close(result));
	}

	return sObjectWrapperTemplate;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

System::Object^
JavascriptInterop::ConvertFromV8(Handle<Value> iValue)
{
	if (iValue->IsNull() || iValue->IsUndefined())
		return nullptr;
	if (iValue->IsBoolean())
		return gcnew System::Boolean(iValue->BooleanValue());
	if (iValue->IsInt32())
		return gcnew System::Int32(iValue->Int32Value());
	if (iValue->IsNumber())
		return gcnew System::Double(iValue->NumberValue());
	if (iValue->IsString())
		return gcnew System::String((wchar_t*)*String::Value(iValue->ToString()));
	if (iValue->IsArray())
		return ConvertArrayFromV8(iValue);
	if (iValue->IsDate())
		return ConvertDateFromV8(iValue);
	if (iValue->IsObject())
	{
		Handle<Object> object = iValue->ToObject();

		if (object->InternalFieldCount() > 0)
			return UnwrapObject(iValue);
		else
			return ConvertObjectFromV8(object);
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<Value>
JavascriptInterop::ConvertToV8(System::Object^ iObject)
{
	if (iObject != nullptr)
	{
		System::Type^ type = iObject->GetType();

		if (type == System::Boolean::typeid)
			return v8::Boolean::New(safe_cast<bool>(iObject));
		if (type == System::Int16::typeid)
			return v8::Int32::New(safe_cast<int>(iObject));
		if (type == System::Int32::typeid)
			return v8::Int32::New(safe_cast<int>(iObject));
		if (type == System::Single::typeid)
			return v8::Number::New(safe_cast<float>(iObject));
		if (type == System::Double::typeid)
			return v8::Number::New(safe_cast<double>(iObject));
		if (type == System::String::typeid)
			return v8::String::New(SystemInterop::ConvertFromSystemString(safe_cast<System::String^>(iObject)).c_str());
		if (type == System::DateTime::typeid)
			return v8::Date::New(SystemInterop::ConvertFromSystemDateTime(safe_cast<System::DateTime^>(iObject)));
		if (type->IsArray)
			return ConvertFromSystemArray(safe_cast<System::Array^>(iObject));
		if (System::Delegate::typeid->IsAssignableFrom(type))
			return ConvertFromSystemDelegate(safe_cast<System::Delegate^>(iObject));
		
		if (type->IsGenericType)
		{
			if(type->GetGenericTypeDefinition() == System::Collections::Generic::Dictionary::typeid)
			{
				return ConvertFromSystemDictionary(iObject);
			}
			if (type->IsGenericType && (type->GetGenericTypeDefinition() == System::Collections::Generic::List::typeid))
				return ConvertFromSystemList(iObject);
		}


		return WrapObject(iObject);
	}

	return Handle<Value>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: should return Handle<External>
Handle<Object>
JavascriptInterop::WrapObject(System::Object^ iObject)
{
	JavascriptContext^ context = JavascriptContext::GetCurrent();

	if (context != nullptr)
	{
		Handle<ObjectTemplate> templ = GetObjectWrapperTemplate();
		Handle<Object> object = templ->NewInstance();
		object->SetInternalField(0, External::New(context->WrapObject(iObject)));

		return object;
	}

	throw gcnew System::Exception("No context currently active.");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: should use Handle<External> iExternal
System::Object^
JavascriptInterop::UnwrapObject(Handle<Value> iValue)
{
	if (iValue->IsObject())
	{
		Handle<Object> object = iValue->ToObject();

		if (object->InternalFieldCount() > 0)
		{
			Handle<External> external = Handle<External>::Cast(object->GetInternalField(0));
			JavascriptExternal* wrapper = (JavascriptExternal*) external->Value();
			return wrapper->GetObject();
		}
	}
	else if (iValue->IsExternal())
	{
		Handle<External> external = Handle<External>::Cast(iValue);
		JavascriptExternal* wrapper = (JavascriptExternal*) external->Value();
		return wrapper->GetObject();
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

System::Object^
JavascriptInterop::ConvertArrayFromV8(Handle<Value> iValue)
{
	v8::Handle<v8::Array> object = v8::Handle<v8::Array>::Cast(iValue->ToObject());
	int length = object->Length();
	cli::array<System::Object^>^ results = gcnew cli::array<System::Object^>(length);

	// Populate the .NET Array with the v8 Array
	for(int i = 0; i < length; i++)
	{
		results->SetValue(ConvertFromV8(object->Get(i)), i);
	}

	return results;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

System::Object^
JavascriptInterop::ConvertObjectFromV8(Handle<Object> iObject)
{
	v8::Local<v8::Array> names = iObject->GetPropertyNames();
	
	unsigned int length = names->Length();
	Dictionary<System::String^, System::Object^>^ results = gcnew Dictionary<System::String^, System::Object^>(length);
	for (unsigned int i = 0; i < length; i++) {
		v8::Handle<v8::Value> nameKey = v8::Uint32::New(i);
		v8::Handle<v8::Value> propName = names->Get(nameKey);
		v8::Handle<v8::Value> propValue = iObject->Get(propName);

		System::String^ key = safe_cast<System::String^>(ConvertFromV8(propName));
		results[key] = ConvertFromV8(propValue);
	}

	return results;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

System::DateTime^
JavascriptInterop::ConvertDateFromV8(Handle<Value> iValue)
{
	System::DateTime^ startDate = gcnew System::DateTime(1970, 1, 2);
	double milliseconds = iValue->NumberValue();
	System::TimeSpan^ timespan = System::TimeSpan::FromMilliseconds(milliseconds);
	return System::DateTime(timespan->Ticks + startDate->Ticks).ToLocalTime();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value>
JavascriptInterop::ConvertFromSystemArray(System::Array^ iArray) 
{
	int lenght = iArray->Length;
	v8::Handle<v8::Array> result = v8::Array::New();
	
	// Transform the .NET array into a Javascript array 
	for (int i = 0; i < lenght; i++) 
	{
		v8::Handle<v8::Value> key = v8::Int32::New(i);
		result->Set(key, ConvertToV8(iArray->GetValue(i)));
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value>
JavascriptInterop::ConvertFromSystemDictionary(System::Object^ iObject) 
{
	v8::Handle<v8::Object> object = v8::Object::New();
	System::Collections::IDictionary^ dictionary =  safe_cast<System::Collections::IDictionary^>(iObject);

	for each(System::Object^ keyValue in dictionary->Keys) 
	{
		v8::Handle<v8::Value> key = ConvertToV8(keyValue);
		v8::Handle<v8::Value> val = ConvertToV8(dictionary[keyValue]);
		object->Set(key, val);
	} 

	return object;
}	

////////////////////////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value>
JavascriptInterop::ConvertFromSystemList(System::Object^ iObject) 
{
	v8::Handle<v8::Array> object = v8::Array::New();
	System::Collections::IList^ list =  safe_cast<System::Collections::IList^>(iObject);

	for(int i = 0; i < list->Count; i++) 
	{
		v8::Handle<v8::Value> key = v8::Int32::New(i);
		v8::Handle<v8::Value> val = ConvertToV8(list[i]);
		object->Set(key, val);
	} 

	return object;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value>
JavascriptInterop::ConvertFromSystemDelegate(System::Delegate^ iDelegate) 
{
	JavascriptContext^ context = JavascriptContext::GetCurrent();
	v8::Handle<v8::External> external = v8::External::New(context->WrapObject(iDelegate));

	v8::Handle<v8::FunctionTemplate> method = v8::FunctionTemplate::New(DelegateInvoker, external);
	return method->GetFunction();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> 
JavascriptInterop::DelegateInvoker(const v8::Arguments& info) {
	JavascriptExternal* wrapper = (JavascriptExternal*)v8::Handle<v8::External>::Cast(info.Data())->Value();
	System::Object^ object = wrapper->GetObject();

	int length = info.Length();
	cli::array<System::Object^>^ args = gcnew cli::array<System::Object^>(length);
	cli::array<System::Type^>^ argTypes = gcnew cli::array<System::Type^>(length);
	
	for (int i = 0; i < length; i++) 
	{
		System::Object^ arg = ConvertFromV8(info[i]);
		args[i] = arg;
	}

	System::Object^ value = static_cast<System::Delegate^>(object)->DynamicInvoke(args);
	return ConvertToV8(value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<Value>
JavascriptInterop::Getter(Local<String> iName, const AccessorInfo &iInfo)
{
	Handle<External> external = Handle<External>::Cast(iInfo.Holder()->GetInternalField(0));
	JavascriptExternal* wrapper = (JavascriptExternal*) external->Value();
	Handle<Function> function;
	Handle<Value> value;

	// get method
	function = wrapper->GetMethod(iName);
	if (!function.IsEmpty())
		return function;

	// get property
	value = wrapper->GetProperty(iName);
	if (!value.IsEmpty())
		return value;

	// member not found
	return Handle<Value>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<Value>
JavascriptInterop::Setter(Local<String> iName, Local<Value> iValue, const AccessorInfo& iInfo)
{
	Handle<External> external = Handle<External>::Cast(iInfo.Holder()->GetInternalField(0));
	JavascriptExternal* wrapper = (JavascriptExternal*) external->Value();
	Handle<Function> function;
	Handle<Value> value;
	
	// set property
	value = wrapper->SetProperty(iName, iValue);

	if (!value.IsEmpty())
		return value;

	// member not found
	return Handle<Value>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<Value>
JavascriptInterop::IndexGetter(uint32_t iIndex, const AccessorInfo &iInfo)
{
	Handle<External> external = Handle<External>::Cast(iInfo.Holder()->GetInternalField(0));
	JavascriptExternal* wrapper = (JavascriptExternal*) external->Value();
	Handle<Value> value;

	// get property
	value = wrapper->GetProperty(iIndex);
	if (!value.IsEmpty())
		return value;

	// member not found
	return Handle<Value>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<Value>
JavascriptInterop::IndexSetter(uint32_t iIndex, Local<Value> iValue, const AccessorInfo &iInfo)
{
	Handle<External> external = Handle<External>::Cast(iInfo.Holder()->GetInternalField(0));
	JavascriptExternal* wrapper = (JavascriptExternal*) external->Value();
	Handle<Value> value;

	// get property
	value = wrapper->SetProperty(iIndex, iValue);
	if (!value.IsEmpty())
		return value;

	// member not found
	return Handle<Value>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Handle<Value>
JavascriptInterop::Invoker(const v8::Arguments& iArgs)
{
	System::Object^ data = UnwrapObject(Handle<External>::Cast(iArgs.Data()));
	System::Reflection::MethodInfo^ bestMethod;
	cli::array<System::Object^>^ suppliedArguments;
	cli::array<System::Object^>^ bestMethodArguments;
	cli::array<System::Object^>^ objectInfo;
	int bestMethodMatchedArgs = -1;
	System::Object^ ret;

	// get target and member's name
	objectInfo = safe_cast<cli::array<System::Object^>^>(data);
	System::Object^ self = objectInfo[0];
	// System::Object^ holder = UnwrapObject(iArgs.Holder());
	System::Type^ holderType = self->GetType(); 
	
	// get members
	System::Type^ type = self->GetType();
	System::String^ memberName = (System::String^)objectInfo[1];
	cli::array<System::Reflection::MemberInfo^>^ members = type->GetMember(memberName);

	if (members->Length > 0 && members[0]->MemberType == System::Reflection::MemberTypes::Method)
	{
		// parameters
		suppliedArguments = gcnew cli::array<System::Object^>(iArgs.Length());
		for (int i = 0; i < iArgs.Length(); i++)
			suppliedArguments[i] = ConvertFromV8(iArgs[i]);
		
		// look for best matching method
		for (int i = 0; i < members->Length; i++)
		{
			System::Reflection::MethodInfo^ method = (System::Reflection::MethodInfo^) members[i];
			cli::array<System::Reflection::ParameterInfo^>^ parametersInfo = method->GetParameters();
			cli::array<System::Object^>^ arguments;

			// match arguments & parameters counts
			if (iArgs.Length() == parametersInfo->Length)
			{
				int match = 0;
				int failed = 0;

				// match parameters
				arguments = gcnew cli::array<System::Object^>(iArgs.Length());
				for (int p = 0; p < suppliedArguments->Length; p++)
				{
					System::Type^ type = parametersInfo[p]->ParameterType;
					System::Object^ arg;

					if (suppliedArguments[p] != nullptr)
					{
						if (suppliedArguments[p]->GetType() == type)
							match++;

						arg = SystemInterop::ConvertToType(suppliedArguments[p], type);
						if (arg == nullptr)
						{
							failed++;
							break;
						}

						arguments[p] = arg;
					}
				}

				// skip if a conversion failed
				if (failed > 0)
					continue;

				// remember best match
				if (match > bestMethodMatchedArgs)
				{
					bestMethod = method;
					bestMethodArguments = arguments;
					bestMethodMatchedArgs = match;
				}

				// skip lookup if all args matched
				if (match == arguments->Length)
					break;
			}
		}
	}

	try
	{
		// invoke
		ret = bestMethod->Invoke(self, bestMethodArguments);
	}
	catch(System::Exception^ Exception)
	{
		v8::ThrowException(JavascriptInterop::ConvertToV8(Exception));
	}

	// return value
	return ConvertToV8(ret);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} } // namespace Noesis::Javascript

////////////////////////////////////////////////////////////////////////////////////////////////////