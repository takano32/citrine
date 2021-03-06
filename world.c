#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include "citrine.h"
#include "siphash.h"


/**
 * @internal
 *
 * ReadFile
 *
 * Reads in an entire file.
 */
char* ctr_internal_readf(char* file_name, uint64_t* total_size) {
   char* prg;
   char ch;
   int prev;
   uint64_t size;
   uint64_t real_size;
   FILE* fp;
   fp = fopen(file_name,"r");
   if( fp == NULL ) {
      printf("Error while opening the file.\n");
      exit(1);
   }
   prev = ftell(fp);
   fseek(fp,0L,SEEK_END);
   size = ftell(fp);
   fseek(fp,prev,SEEK_SET);
   real_size = (size+4)*sizeof(char);
   prg = ctr_heap_allocate(real_size); /* add 4 bytes, 3 for optional closing sequence verbatim mode and one lucky byte! */
   ctr_program_length=0;
   while( ( ch = fgetc(fp) ) != EOF ) prg[ctr_program_length++]=ch;
   fclose(fp);
   *total_size = (uint64_t) real_size;
   return prg;
}

/**
 * @internal
 *
 * InternalObjectIsEqual
 *
 * Detemines whether two objects are identical.
 */
int ctr_internal_object_is_equal(ctr_object* object1, ctr_object* object2) {
	char* string1;
	char* string2;
	ctr_size len1;
	ctr_size len2;
	ctr_size d;
	if (object1->info.type == CTR_OBJECT_TYPE_OTSTRING && object2->info.type == CTR_OBJECT_TYPE_OTSTRING) {
		string1 = object1->value.svalue->value;
		string2 = object2->value.svalue->value;
		len1 = object1->value.svalue->vlen;
		len2 = object2->value.svalue->vlen;
		if (len1 != len2) return 0;
		d = memcmp(string1, string2, len1);
		if (d==0) return 1;
		return 0;
	}
	if (object1->info.type == CTR_OBJECT_TYPE_OTNUMBER && object2->info.type == CTR_OBJECT_TYPE_OTNUMBER) {
		ctr_number num1 = object1->value.nvalue;
		ctr_number num2 = object2->value.nvalue;
		if (num1 == num2) return 1;
		return 0;
	}
	if (object1->info.type == CTR_OBJECT_TYPE_OTBOOL && object2->info.type == CTR_OBJECT_TYPE_OTBOOL) {
		int b1 = object1->value.bvalue;
		int b2 = object2->value.bvalue;
		if (b1 == b2) return 1;
		return 0;
	}
	if (object1 == object2) return 1;
	return 0;		
}

/**
 * @internal
 *
 * InternalObjectIndexHash
 *
 * Given an object, this function will calculate a hash to speed-up
 * lookup.
 */
uint64_t ctr_internal_index_hash(ctr_object* key) {
	ctr_object* stringKey = ctr_internal_cast2string(key);
	return siphash24(stringKey->value.svalue->value, stringKey->value.svalue->vlen, CtrHashKey);
}

/**
 * @internal
 *
 * InternalObjectFindProperty
 *
 * Finds property in object.
 */
ctr_object* ctr_internal_object_find_property(ctr_object* owner, ctr_object* key, int is_method) {
	ctr_mapitem* head;
	uint64_t hashKey = ctr_internal_index_hash(key);

	if (is_method) {
		if (owner->methods->size == 0) {
			return NULL;
		}
		head = owner->methods->head; 
	} else {
		if (owner->properties->size == 0) {
			return NULL;
		}
		head = owner->properties->head; 
	}
	while(head) {
		if ((hashKey == head->hashKey) && ctr_internal_object_is_equal(head->key, key)) {
			return head->value;
		}
		head = head->next;
	}
	return NULL;
}


/**
 * @internal
 *
 * InternalObjectDeleteProperty
 *
 * Deletes the specified property from the object.
 */
void ctr_internal_object_delete_property(ctr_object* owner, ctr_object* key, int is_method) {
	uint64_t hashKey = ctr_internal_index_hash(key);
	ctr_mapitem* head;
	ctr_mapitem* oldMapItem;
	if (is_method) {
		if (owner->methods->size == 0) {
			return;
		}
		head = owner->methods->head; 
	} else {
		if (owner->properties->size == 0) {
			return;
		}
		head = owner->properties->head; 
	}
	while(head) {
		if ((hashKey == head->hashKey) && ctr_internal_object_is_equal(head->key, key)) {
			oldMapItem = head;
			if (head->next && head->prev) {
				head->next->prev = head->prev;
				head->prev->next = head->next;
			} else {
				if (head->next) {
					head->next->prev = NULL;
				}
				if (head->prev) {
					head->prev->next = NULL;
				}
			}
			if (is_method) {
				if (owner->methods->head == head) {
					if (head->next) {
						owner->methods->head = head->next;
					} else {
						owner->methods->head = NULL;
					}
				}
				owner->methods->size --; 
			} else {
				if (owner->properties->head == head) {
					if (head->next) {
						owner->properties->head = head->next;
					} else {
						owner->properties->head = NULL;
					}
				}
				owner->properties->size --;
			}
			ctr_heap_free( head );
			return;
		}
		head = head->next;
	}
	return;
}

/**
 * @internal
 *
 * InternalObjectAddProperty
 *
 * Adds a property to an object.
 */
void ctr_internal_object_add_property(ctr_object* owner, ctr_object* key, ctr_object* value, int m) {
	ctr_mapitem* new_item = ctr_heap_allocate(sizeof(ctr_mapitem));
	ctr_mapitem* current_head = NULL;
	new_item->key = key;
	new_item->hashKey = ctr_internal_index_hash(key);
	new_item->value = value;
	new_item->next = NULL;
	new_item->prev = NULL;
	if (m) {
		if (owner->methods->size == 0) {
			owner->methods->head = new_item;
		} else {
			current_head = owner->methods->head;
			current_head->prev = new_item;
			new_item->next = current_head;
			owner->methods->head = new_item;
		}
		owner->methods->size ++;
	} else {
		if (owner->properties->size == 0) {
			owner->properties->head = new_item;
		} else {
			current_head = owner->properties->head;
			current_head->prev = new_item;
			new_item->next = current_head;
			owner->properties->head = new_item;
		}
		owner->properties->size ++;
	}
}

/**
 * @internal
 *
 * InternalObjectSetProperty
 *
 * Sets a property on an object.
 */ 
void ctr_internal_object_set_property(ctr_object* owner, ctr_object* key, ctr_object* value, int is_method) {
	ctr_internal_object_delete_property(owner, key, is_method);
	ctr_internal_object_add_property(owner, key, value, is_method);
}

/**
 * @internal
 *
 * InternalMemMem
 *
 * memmem implementation because this not available on every system.
 */
char* ctr_internal_memmem(char* haystack, long hlen, char* needle, long nlen, int reverse ) {
	char* cur;
	char* last;
	char* begin;
	int step = (1 - reverse * 2); /* 1 if reverse = 0, -1 if reverse = 1 */
	if (nlen == 0) return NULL;
	if (hlen == 0) return NULL;
	if (hlen < nlen) return NULL;
	if (!reverse) {
		begin = haystack;
		last = haystack + hlen - nlen + 1;
	} else {
		begin = haystack + hlen - nlen;
		last = haystack - 1;
	}
	char* q = calloc( sizeof(char), nlen );
	for(cur = begin; cur!=last; cur += step) {
		if (memcmp(cur,needle,nlen) == 0) return cur;
	}
	return NULL;
}

/**
 * @internal
 *
 * InternalObjectCreate
 *
 * Creates an object.
 */
ctr_object* ctr_internal_create_object(int type) {
	ctr_object* o;
	/**
	 * If there's a second hand object available at the junkyard,
	 * prefer that. Saves a call to malloc.
	 */
	if (ctr_gc_junk_counter > 0) {
		o = ctr_gc_junkyard[--ctr_gc_junk_counter];
	} else {
		o = ctr_heap_allocate(sizeof(ctr_object));
	}
	o->properties = ctr_heap_allocate(sizeof(ctr_map));
	o->methods = ctr_heap_allocate(sizeof(ctr_map));
	o->properties->size = 0;
	o->methods->size = 0;
	o->properties->head = NULL;
	o->methods->head = NULL;
	o->info.type = type;
	o->info.sticky = 0;
	o->info.mark = 0;
	if (type==CTR_OBJECT_TYPE_OTBOOL) o->value.bvalue = 0;
	if (type==CTR_OBJECT_TYPE_OTNUMBER) o->value.nvalue = 0;
	if (type==CTR_OBJECT_TYPE_OTSTRING) {
		o->value.svalue = ctr_heap_allocate(sizeof(ctr_string));
		o->value.svalue->value = "";
		o->value.svalue->vlen = 0;
	}
	o->gnext = NULL;
	if (ctr_first_object == NULL) {
		ctr_first_object = o;
	} else {
		o->gnext = ctr_first_object;
		ctr_first_object = o;
	}
	return o;
}

/**
 * @internal
 *
 * InternalFunctionCreate
 *
 * Create a function and add this to the object as a method.
 */
void ctr_internal_create_func(ctr_object* o, ctr_object* key, ctr_object* (*func)( ctr_object*, ctr_argument* ) ) {
	ctr_object* methodObject = ctr_internal_create_object(CTR_OBJECT_TYPE_OTNATFUNC);
	methodObject->value.fvalue = func;
	ctr_internal_object_add_property(o, key, methodObject, 1);
}

/**
 * @internal
 *
 * InternalNumberCast
 *
 * Casts an object to a number object.
 */
ctr_object* ctr_internal_cast2number(ctr_object* o) {
	if (o->info.type == CTR_OBJECT_TYPE_OTNUMBER) return o;
	if (o->info.type == CTR_OBJECT_TYPE_OTSTRING) {
		return ctr_build_number_from_string(o->value.svalue->value, o->value.svalue->vlen);
	}
	return ctr_build_number_from_float((ctr_number)0);
}

/**
 * @internal
 *
 * InternalStringCast
 *
 * Casts an object to a string object.
 */
ctr_object* ctr_internal_cast2string( ctr_object* o ) {
	int slen;
	char* s;
	char* p;
	char* buf;
	int bufSize;
	ctr_object* stringObject;
	switch (o->info.type) {
		case CTR_OBJECT_TYPE_OTSTRING:
			return o;
			break;
		case CTR_OBJECT_TYPE_OTNIL:
			return ctr_build_string_from_cstring( "[Nil]" );
			break;
		case CTR_OBJECT_TYPE_OTBOOL:
			if (o->value.bvalue == 1) {
				return ctr_build_string_from_cstring( "[True]" );
			} else if (o->value.bvalue == 0) {
				return ctr_build_string_from_cstring( "[False]" );
			} else {
				return ctr_build_string_from_cstring( "[?]" );
			}
			break;
		case CTR_OBJECT_TYPE_OTNUMBER:
			s = ctr_heap_allocate( 80 * sizeof( char ) );
			bufSize = 100 * sizeof( char );
			buf = ctr_heap_allocate( bufSize );
			snprintf( buf, 99, "%.10f", o->value.nvalue );
			p = buf + strlen(buf) - 1;
			while ( *p == '0' && *p-- != '.' );
			*( p + 1 ) = '\0';
			if ( *p == '.' ) *p = '\0';
			strncpy( s, buf, strlen( buf ) );
			ctr_heap_free( buf );
			slen = strlen(s);
			stringObject = ctr_build_string(s, slen);
			ctr_heap_free( s );
			return stringObject;
			break;
		case CTR_OBJECT_TYPE_OTBLOCK:
			return ctr_build_string_from_cstring( "[Block]" );
			break;
		case CTR_OBJECT_TYPE_OTOBJECT:
			return ctr_build_string_from_cstring( "[Object]" );
			break;
		default:
			break;
	}
	return ctr_build_string_from_cstring( "[?]" );
}

/**
 * @internal
 *
 * InternalBooleanCast
 *
 * Casts an object to a boolean.
 */
ctr_object* ctr_internal_cast2bool( ctr_object* o ) {
	if (o->info.type == CTR_OBJECT_TYPE_OTBOOL) return o;
	if (o->info.type == CTR_OBJECT_TYPE_OTNIL
		|| (o->info.type == CTR_OBJECT_TYPE_OTNUMBER && o->value.nvalue == 0)
		|| (o->info.type == CTR_OBJECT_TYPE_OTSTRING && o->value.svalue->vlen == 0)) return ctr_build_bool(0);
	return ctr_build_bool(1);
}

/**
 * @internal
 *
 * ContextOpen
 *
 * Opens a new context to keep track of variables.
 */
void ctr_open_context() {
	ctr_object* context;
	ctr_context_id++;
	if (ctr_context_id > 299) {
		CtrStdFlow = ctr_build_string_from_cstring( "Too many nested calls." );
		CtrStdFlow->info.sticky = 1;
	}
	if (ctr_context_id > 300) {
		printf("Too many nested calls.\n");
		exit(1);
	}
	context = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	context->info.sticky = 1;
	ctr_contexts[ctr_context_id] = context;
}

/**
 * @internal
 *
 * ContextClose
 *
 * Closes a context.
 */
void ctr_close_context() {
	ctr_contexts[ctr_context_id]->info.sticky = 0;
	if (ctr_context_id == 0) return;
	ctr_context_id--;
}

/**
 * @internal
 *
 * CTRFind
 *
 * Tries to locate a variable in the current context or one
 * of the contexts beneath.
 */
ctr_object* ctr_find(ctr_object* key) {
	int i = ctr_context_id;
	ctr_object* foundObject = NULL;
	if (CtrStdFlow) return CtrStdNil;
	while((i>-1 && foundObject == NULL)) {
		ctr_object* context = ctr_contexts[i];
		foundObject = ctr_internal_object_find_property(context, key, 0);
		i--;
	}
	if (foundObject == NULL) {
		ctr_internal_plugin_find(key);
		foundObject = ctr_internal_object_find_property(CtrStdWorld, key, 0);
	}
	if (foundObject == NULL) {
		char* key_name;
		char* message;
		char* full_message;
		int message_size;
		message = "Key not found: ";
		message_size = ((strlen(message))+key->value.svalue->vlen);
		full_message = ctr_heap_allocate( message_size * sizeof( char ) );
		key_name = ctr_heap_allocate_cstring( key );
		memcpy(full_message, message, strlen(message));
		memcpy(full_message + strlen(message), key_name, key->value.svalue->vlen);
		CtrStdFlow = ctr_build_string(full_message, message_size);
		CtrStdFlow->info.sticky = 1;
		ctr_heap_free( full_message );
		ctr_heap_free( key_name );
		return CtrStdNil;
	}
	return foundObject;
}

/**
 * @internal
 *
 * CTRFindInMy
 *
 * Tries to locate a property of an object.
 */
ctr_object* ctr_find_in_my(ctr_object* key) {
	ctr_object* context = ctr_find(ctr_build_string_from_cstring( "me" ) );
	ctr_object* foundObject = ctr_internal_object_find_property(context, key, 0);
	if (CtrStdFlow) return CtrStdNil;
	if (foundObject == NULL) {
		char* key_name;
		char* message;
		char* full_message;
		int message_size;
		message = "Object property not found: ";
		message_size = ((strlen(message))+key->value.svalue->vlen);
		full_message = ctr_heap_allocate( message_size * sizeof( char ) );
		key_name = ctr_heap_allocate_cstring( key );
		memcpy(full_message, message, strlen(message));
		memcpy(full_message + strlen(message), key_name, key->value.svalue->vlen);
		CtrStdFlow = ctr_build_string(full_message, message_size);
		CtrStdFlow->info.sticky = 1;
		ctr_heap_free( full_message );
		ctr_heap_free( key_name );
		return CtrStdNil;
	}
	return foundObject;
}

/**
 * @internal
 *
 * CTRSetBasic
 *
 * Sets a proeprty in an object (context).
 */
void ctr_set(ctr_object* key, ctr_object* object) {
	int i = ctr_context_id;
	ctr_object* context;
	ctr_object* foundObject = NULL;
	if (ctr_contexts[ctr_context_id] == CtrStdWorld) {
		ctr_internal_object_set_property(ctr_contexts[ctr_context_id], key, object, 0);
		return;
	}
	while((i>-1 && foundObject == NULL)) {
		context = ctr_contexts[i];
		foundObject = ctr_internal_object_find_property(context, key, 0);
		if (foundObject) break;
		i--;
	}
	if (!foundObject) {
		char* key_name;
		char* message;
		char* full_message;
		int message_size;
		message = "Cannot assign to undefined variable: ";
		message_size = ((strlen(message))+key->value.svalue->vlen);
		full_message = ctr_heap_allocate(message_size*sizeof(char));
		key_name = ctr_heap_allocate_cstring( key );
		memcpy(full_message, message, strlen(message));
		memcpy(full_message + strlen(message), key_name, key->value.svalue->vlen);
		CtrStdFlow = ctr_build_string(full_message, message_size);
		CtrStdFlow->info.sticky = 1;
		ctr_heap_free( full_message );
		ctr_heap_free( key_name );
		return;
	}
	ctr_internal_object_set_property(context, key, object, 0);
}

/**
 * @internal
 *
 * WorldInitialize
 *
 * Populate the World of Citrine.
 */
void ctr_initialize_world() {
	int i;
	srand((unsigned)time(NULL));
	for(i=0; i<16; i++) {
		CtrHashKey[i] = (rand() % 255);
	}
	ctr_first_object = NULL;
	CtrStdWorld = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	CtrStdWorld->info.sticky = 1;
	ctr_contexts[0] = CtrStdWorld;

	/* Object */
	CtrStdObject = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "new" ), &ctr_object_make );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "equals:" ), &ctr_object_equals );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "="), &ctr_object_equals );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "on:do:" ), &ctr_object_on_do );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "respondTo:" ), &ctr_object_respond );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "respondTo:with:" ), &ctr_object_respond );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "respondTo:with:and:" ), &ctr_object_respond );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "type" ), &ctr_object_type );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "isNil" ), &ctr_object_is_nil );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "myself" ), &ctr_object_myself );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "do" ), &ctr_object_do );
	ctr_internal_create_func( CtrStdObject, ctr_build_string_from_cstring( "done" ), &ctr_object_done );
	ctr_internal_object_add_property( CtrStdWorld, ctr_build_string_from_cstring( "Object" ), CtrStdObject, 0 );
	CtrStdObject->link = NULL;
	CtrStdObject->info.sticky = 1;

	/* Nil */
	CtrStdNil = ctr_internal_create_object(CTR_OBJECT_TYPE_OTNIL);
	ctr_internal_object_add_property( CtrStdWorld, ctr_build_string_from_cstring( "Nil" ), CtrStdNil, 0 );
	ctr_internal_create_func( CtrStdNil, ctr_build_string_from_cstring( "isNil" ), &ctr_nil_is_nil );
	CtrStdNil->link = CtrStdObject;
	CtrStdNil->info.sticky = 1;
	
	/* Boolean */
	CtrStdBool = ctr_internal_create_object(CTR_OBJECT_TYPE_OTBOOL);
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "ifTrue:" ), &ctr_bool_iftrue );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "ifFalse:" ), &ctr_bool_ifFalse );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "break" ), &ctr_bool_break );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "continue" ), &ctr_bool_continue );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "else:" ), &ctr_bool_ifFalse );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "not" ), &ctr_bool_not );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "&" ), &ctr_bool_and );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "!" ), &ctr_bool_nor );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "|" ), &ctr_bool_or );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "?" ), &ctr_bool_xor );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "=" ),&ctr_bool_eq );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "!=:" ), &ctr_bool_neq );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "toNumber" ), &ctr_bool_to_number );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "toString" ), &ctr_bool_to_string );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "flip" ), &ctr_bool_flip );
	ctr_internal_create_func( CtrStdBool, ctr_build_string_from_cstring( "either:or:" ), &ctr_bool_either_or );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring("Boolean" ), CtrStdBool, 0 );
	CtrStdBool->link = CtrStdObject;
	CtrStdBool->info.sticky = 1;

	/* Number */
	CtrStdNumber = ctr_internal_create_object(CTR_OBJECT_TYPE_OTNUMBER);
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "to:step:do:" ), &ctr_number_to_step_do );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "+" ), &ctr_number_add );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "+=:" ), &ctr_number_inc );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "-" ), &ctr_number_minus );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "-=:" ), &ctr_number_dec );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "*" ),&ctr_number_multiply );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "times:" ),&ctr_number_times );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "*=:" ),&ctr_number_mul );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "/" ), &ctr_number_divide );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "/=:" ), &ctr_number_div );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( ">" ), &ctr_number_higherThan );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( ">=:" ), &ctr_number_higherEqThan );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "<" ), &ctr_number_lowerThan );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "<=:" ), &ctr_number_lowerEqThan );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "=" ), &ctr_number_eq );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "!=:" ), &ctr_number_neq );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "%" ), &ctr_number_modulo );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "factorial" ), &ctr_number_factorial );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "floor" ), &ctr_number_floor );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "ceil" ), &ctr_number_ceil );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "round" ), &ctr_number_round );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "abs" ), &ctr_number_abs );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "sin" ), &ctr_number_sin );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "cos" ), &ctr_number_cos );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "exp" ), &ctr_number_exp );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "sqrt" ), &ctr_number_sqrt );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "tan" ), &ctr_number_tan );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "atan" ), &ctr_number_atan );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "log" ), &ctr_number_log );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "toPowerOf:" ), &ctr_number_pow );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "min:" ), &ctr_number_min );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "max:" ), &ctr_number_max );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "odd" ), &ctr_number_odd );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "even" ), &ctr_number_even );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "pos" ), &ctr_number_positive );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "neg" ), &ctr_number_negative );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "toString" ), &ctr_number_to_string );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "toBoolean" ), &ctr_number_to_boolean );
	ctr_internal_create_func(CtrStdNumber, ctr_build_string_from_cstring( "between:and:" ),&ctr_number_between );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Number" ), CtrStdNumber, 0);
	CtrStdNumber->link = CtrStdObject;
	CtrStdNumber->info.sticky = 1;

	/* String */
	CtrStdString = ctr_internal_create_object(CTR_OBJECT_TYPE_OTSTRING);
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "bytes" ), &ctr_string_bytes );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "length" ), &ctr_string_length );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "from:to:" ), &ctr_string_fromto );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "from:length:" ), &ctr_string_from_length );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "+" ), &ctr_string_concat );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "=" ), &ctr_string_eq );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "!=:" ), &ctr_string_neq );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "trim" ), &ctr_string_trim );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "leftTrim" ), &ctr_string_ltrim );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "rightTrim" ), &ctr_string_rtrim );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "htmlEscape" ), &ctr_string_html_escape );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "at:" ), &ctr_string_at );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "@" ), &ctr_string_at );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "byteAt:" ), &ctr_string_byte_at );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "indexOf:" ), &ctr_string_index_of );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "lastIndexOf:" ), &ctr_string_last_index_of );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "replace:with:" ), &ctr_string_replace_with );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "split:" ), &ctr_string_split );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "asciiUpperCase" ), &ctr_string_to_upper );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "asciiLowerCase" ), &ctr_string_to_lower );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "asciiUpperCase1st" ), &ctr_string_to_upper1st );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "asciiLowerCase1st" ), &ctr_string_to_lower1st );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "skip:" ), &ctr_string_skip );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "append:" ), &ctr_string_append );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "toNumber" ), &ctr_string_to_number );
	ctr_internal_create_func(CtrStdString, ctr_build_string_from_cstring( "toBoolean" ), &ctr_string_to_boolean );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "String" ), CtrStdString, 0 );
	CtrStdString->link = CtrStdObject;
	CtrStdString->info.sticky = 1;

	/* Block */
	CtrStdBlock = ctr_internal_create_object(CTR_OBJECT_TYPE_OTBLOCK);
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "run" ), &ctr_block_runIt );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "applyTo:" ), &ctr_block_runIt );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "applyTo:and:" ), &ctr_block_runIt );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "applyTo:and:and:" ), &ctr_block_runIt );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "set:value:" ), &ctr_block_set );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "error:" ), &ctr_block_error );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "catch:" ), &ctr_block_catch );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "whileTrue:" ), &ctr_block_while_true );
	ctr_internal_create_func(CtrStdBlock, ctr_build_string_from_cstring( "whileFalse:" ), &ctr_block_while_false );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring("CodeBlock" ), CtrStdBlock, 0 );
	CtrStdBlock->link = CtrStdObject;
	CtrStdBlock->info.sticky = 1;

	/* Array */
	CtrStdArray = ctr_array_new(CtrStdObject, NULL);
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "new" ), &ctr_array_new );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "<" ), &ctr_array_new_and_push );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "push:" ), &ctr_array_push );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( ";" ), &ctr_array_push );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "unshift:" ), &ctr_array_unshift );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "shift" ), &ctr_array_shift );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "count" ), &ctr_array_count );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "join:" ), &ctr_array_join );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "pop" ), &ctr_array_pop );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "at:" ), &ctr_array_get );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "@" ), &ctr_array_get );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "sort:" ), &ctr_array_sort );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "put:at:" ), &ctr_array_put );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "from:length:" ), &ctr_array_from_length );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "+" ), &ctr_array_add );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "map:" ), &ctr_array_map );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "each:" ), &ctr_array_map );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "min" ), &ctr_array_min );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "max" ), &ctr_array_max );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "sum" ), &ctr_array_sum );
	ctr_internal_create_func(CtrStdArray, ctr_build_string_from_cstring( "product" ), &ctr_array_product );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Array" ), CtrStdArray, 0 );
	CtrStdArray->link = CtrStdObject;
	CtrStdArray->info.sticky = 1;

	/* Map */
	CtrStdMap = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "new" ), &ctr_map_new );
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "put:at:" ), &ctr_map_put );
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "at:" ), &ctr_map_get );
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "@" ), &ctr_map_get );
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "count" ), &ctr_map_count );
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "each:" ), &ctr_map_each );
	ctr_internal_create_func(CtrStdMap, ctr_build_string_from_cstring( "map:" ), &ctr_map_each );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Map" ), CtrStdMap, 0 );
	CtrStdMap->link = CtrStdObject;
	CtrStdMap->info.sticky = 1;

	/* Console */
	CtrStdConsole = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdConsole, ctr_build_string_from_cstring("write:" ), &ctr_console_write );
	ctr_internal_create_func(CtrStdConsole, ctr_build_string_from_cstring(">" ), &ctr_console_write );
	ctr_internal_create_func(CtrStdConsole, ctr_build_string_from_cstring("brk" ), &ctr_console_brk );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring("Pen" ), CtrStdConsole, 0 );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring("?" ), CtrStdConsole, 0 );
	CtrStdConsole->link = CtrStdObject;
	CtrStdConsole->info.sticky = 1;

	/* File */
	CtrStdFile = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	CtrStdFile->value.rvalue = NULL;
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "new:" ), &ctr_file_new );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "path" ), &ctr_file_path );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "read" ), &ctr_file_read );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "write:" ), &ctr_file_write );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "append:" ), &ctr_file_append );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "exists" ), &ctr_file_exists );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "size" ), &ctr_file_size );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "delete" ), &ctr_file_delete );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "include" ), &ctr_file_include );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "open:" ), &ctr_file_open );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "close" ), &ctr_file_close );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "readBytes:" ), &ctr_file_read_bytes );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "writeBytes:" ), &ctr_file_write_bytes );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "seek:" ), &ctr_file_seek );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "rewind" ), &ctr_file_seek_rewind );
	ctr_internal_create_func(CtrStdFile, ctr_build_string_from_cstring( "end" ), &ctr_file_seek_end );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "File" ), CtrStdFile, 0);
	CtrStdFile->link = CtrStdObject;
	CtrStdFile->info.sticky = 1;

	/* Command */
	CtrStdCommand = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "argument:" ), &ctr_command_argument );
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "argCount" ), &ctr_command_num_of_args );
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "env:" ), &ctr_command_get_env );
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "env:val:" ), &ctr_command_set_env );
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "waitForInput" ), &ctr_command_question );
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "exit" ), &ctr_command_exit );
	ctr_internal_create_func(CtrStdCommand, ctr_build_string_from_cstring( "flush" ), &ctr_command_flush );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Program" ), CtrStdCommand, 0 );
	CtrStdCommand->link = CtrStdObject;
	CtrStdCommand->info.sticky = 1;

	/* Clock */
	CtrStdClock = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdClock, ctr_build_string_from_cstring( "wait:" ), &ctr_clock_wait );
	ctr_internal_create_func(CtrStdClock, ctr_build_string_from_cstring( "time" ), &ctr_clock_time );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Clock" ), CtrStdClock, 0 );
	CtrStdClock->link = CtrStdObject;
	CtrStdFile->info.sticky = 1;

	/* Dice */
	CtrStdDice = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdDice, ctr_build_string_from_cstring( "roll" ), &ctr_dice_throw );
	ctr_internal_create_func(CtrStdDice, ctr_build_string_from_cstring( "rollWithSides:" ), &ctr_dice_sides );
	ctr_internal_create_func(CtrStdDice, ctr_build_string_from_cstring( "rawRandomNumber" ), &ctr_dice_rand );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Dice" ), CtrStdDice, 0 );
	CtrStdDice->link = CtrStdObject;
	CtrStdDice->info.sticky = 1;

	/* Shell */
	CtrStdShell = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdShell, ctr_build_string_from_cstring( "respondTo:" ), &ctr_shell_respond_to );
	ctr_internal_create_func(CtrStdShell, ctr_build_string_from_cstring( "respondTo:with:" ), &ctr_shell_respond_to_with );
	ctr_internal_create_func(CtrStdShell, ctr_build_string_from_cstring( "call:" ), &ctr_shell_call );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Shell" ), CtrStdShell, 0 );
	CtrStdShell->link = CtrStdObject;
	CtrStdShell->info.sticky = 1;

	/* Broom */
	CtrStdGC = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "sweep" ), &ctr_gc_collect );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "dust" ), &ctr_gc_dust );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "objectCount" ), &ctr_gc_object_count );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "keptCount" ), &ctr_gc_kept_count );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "keptAlloc" ), &ctr_gc_kept_alloc );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "stickyCount" ), &ctr_gc_sticky_count );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "memoryLimit:" ), &ctr_gc_setmemlimit );
	ctr_internal_create_func(CtrStdGC, ctr_build_string_from_cstring( "mode:" ),  &ctr_gc_setmode );
	ctr_internal_object_add_property(CtrStdWorld, ctr_build_string_from_cstring( "Broom" ), CtrStdGC, 0 );
	CtrStdGC->link = CtrStdObject;
	CtrStdGC->info.sticky = 1;

	/* Other objects */
	CtrStdBreak = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	CtrStdContinue = ctr_internal_create_object(CTR_OBJECT_TYPE_OTOBJECT);
	CtrStdExit = ctr_internal_create_object( CTR_OBJECT_TYPE_OTOBJECT );
	CtrStdBreak->info.sticky = 1;
	CtrStdContinue->info.sticky = 1;
	CtrStdExit->info.sticky = 1;
}

/**
 * @internal
 *
 * CTRMessageSend
 *
 * Sends a message to a receiver object.
 */
ctr_object* ctr_send_message(ctr_object* receiverObject, char* message, long vlen, ctr_argument* argumentList) {
	char toParent = 0;
	ctr_object* me;
	ctr_object* methodObject;
	ctr_object* searchObject;
	ctr_object* returnValue;
	ctr_argument* argCounter;
	ctr_argument* mesgArgument;
	ctr_object* result;
	ctr_object* (*funct)(ctr_object* receiverObject, ctr_argument* argumentList);
	ctr_object* msg = NULL;
	int argCount;
	if (CtrStdFlow != NULL) return CtrStdNil; /* Error mode, ignore subsequent messages until resolved. */
	methodObject = NULL;
	searchObject = receiverObject;
	if (vlen > 1 && message[0] == '`') {
		me = ctr_internal_object_find_property(ctr_contexts[ctr_context_id], ctr_build_string_from_cstring("me"), 0);
		if (searchObject == me) {
			toParent = 1;
			message = message + 1;
			vlen--;
		}
	}
	msg = ctr_build_string(message, vlen);
	msg->info.sticky = 1; /* prevent message from being swept, no need to free(), GC will do */
	while(!methodObject) {
		methodObject = ctr_internal_object_find_property(searchObject, msg, 1);
		if (methodObject && toParent) { toParent = 0; methodObject = NULL; }
		if (methodObject) break;
		if (!searchObject->link) break;
		searchObject = searchObject->link;
	}
	if (!methodObject) {
		argCounter = argumentList;
		argCount = 0;
		while(argCounter->next && argCount < 4) {
			argCounter = argCounter->next;
			argCount ++;
		}
		mesgArgument = (ctr_argument*) ctr_heap_allocate( sizeof( ctr_argument ) );
		mesgArgument->object = ctr_build_string(message, vlen);
		mesgArgument->next = argumentList;
		if (argCount == 0 || argCount > 2) {
			returnValue = ctr_send_message(receiverObject, "respondTo:", 10,  mesgArgument);
		} else if (argCount == 1) {
			returnValue = ctr_send_message(receiverObject, "respondTo:with:", 15,  mesgArgument);
		} else if (argCount == 2) {
			returnValue = ctr_send_message(receiverObject, "respondTo:with:and:", 19,  mesgArgument);
		}
		ctr_heap_free( mesgArgument );
		msg->info.sticky = 0;
		if (receiverObject->info.chainMode == 1) return receiverObject;
		return returnValue;
	}
	if (methodObject->info.type == CTR_OBJECT_TYPE_OTNATFUNC) {
		funct = methodObject->value.fvalue;
		result = funct(receiverObject, argumentList);
	}
	if (methodObject->info.type == CTR_OBJECT_TYPE_OTBLOCK) {
		result = ctr_block_run(methodObject, argumentList, receiverObject);
	}
	if (msg) msg->info.sticky = 0;
	if (receiverObject->info.chainMode == 1) return receiverObject;
	return result;
}


/**
 * @internal
 *
 * CTRValueAssignment
 *
 * Assigns a value to a variable in the current context.
 */
ctr_object* ctr_assign_value(ctr_object* key, ctr_object* o) {
	ctr_object* object = NULL;
	if (CtrStdFlow) return CtrStdNil;
	key->info.sticky = 0;
	switch(o->info.type){
		case CTR_OBJECT_TYPE_OTBOOL:
			object = ctr_build_bool(o->value.bvalue);
			break;
		case CTR_OBJECT_TYPE_OTNUMBER:
			object = ctr_build_number_from_float(o->value.nvalue);
			break;
		case CTR_OBJECT_TYPE_OTSTRING:
			object = ctr_build_string(o->value.svalue->value, o->value.svalue->vlen);
			break;
		case CTR_OBJECT_TYPE_OTNIL:
		case CTR_OBJECT_TYPE_OTNATFUNC:
		case CTR_OBJECT_TYPE_OTOBJECT:
		case CTR_OBJECT_TYPE_OTEX:
		case CTR_OBJECT_TYPE_OTMISC:
		case CTR_OBJECT_TYPE_OTARRAY:
		case CTR_OBJECT_TYPE_OTBLOCK:
			object = o;
			break;
	}
	ctr_set(key, object);
	return object;
}


/**
 * @internal
 *
 * CTRAssignValueObject
 *
 * Assigns a value to a property of an object. 
 */
ctr_object* ctr_assign_value_to_my(ctr_object* key, ctr_object* o) {
	ctr_object* object = NULL;
	ctr_object* my = ctr_find(ctr_build_string_from_cstring( "me" ) );
	if (CtrStdFlow) return CtrStdNil;
	key->info.sticky = 0;
	switch(o->info.type){
		case CTR_OBJECT_TYPE_OTBOOL:
			object = ctr_build_bool(o->value.bvalue);
			break;
		case CTR_OBJECT_TYPE_OTNUMBER:
			object = ctr_build_number_from_float(o->value.nvalue);
			break;
		case CTR_OBJECT_TYPE_OTSTRING:
			object = ctr_build_string(o->value.svalue->value, o->value.svalue->vlen);
			break;
		case CTR_OBJECT_TYPE_OTNIL:
		case CTR_OBJECT_TYPE_OTNATFUNC:
		case CTR_OBJECT_TYPE_OTOBJECT:
		case CTR_OBJECT_TYPE_OTEX:
		case CTR_OBJECT_TYPE_OTMISC:
		case CTR_OBJECT_TYPE_OTARRAY:
		case CTR_OBJECT_TYPE_OTBLOCK:
			object = o;
			break;
	}
	ctr_internal_object_set_property(my, key, object, 0);
	return object;
}

/**
 * @internal
 *
 * CTRAssignValueObjectLocal
 *
 * Assigns a value to a local of an object. 
 */
ctr_object* ctr_assign_value_to_local(ctr_object* key, ctr_object* o) {
	ctr_object* object = NULL;
	ctr_object* context;
	if (CtrStdFlow) return CtrStdNil;
	context = ctr_contexts[ctr_context_id];
	key->info.sticky = 0;
	switch(o->info.type){
		case CTR_OBJECT_TYPE_OTBOOL:
			object = ctr_build_bool(o->value.bvalue);
			break;
		case CTR_OBJECT_TYPE_OTNUMBER:
			object = ctr_build_number_from_float(o->value.nvalue);
			break;
		case CTR_OBJECT_TYPE_OTSTRING:
			object = ctr_build_string(o->value.svalue->value, o->value.svalue->vlen);
			break;
		case CTR_OBJECT_TYPE_OTNIL:
		case CTR_OBJECT_TYPE_OTNATFUNC:
		case CTR_OBJECT_TYPE_OTOBJECT:
		case CTR_OBJECT_TYPE_OTEX:
		case CTR_OBJECT_TYPE_OTMISC:
		case CTR_OBJECT_TYPE_OTARRAY:
		case CTR_OBJECT_TYPE_OTBLOCK:
			object = o;
			break;
	}
	ctr_internal_object_set_property(context, key, object, 0);
	return object;
}

/**
 * @internal
 *
 * CTRAssignValueObjectLocalByRef
 *
 * Assigns a value to a local of an object.
 * Always assigns by reference.
 */
ctr_object* ctr_assign_value_to_local_by_ref(ctr_object* key, ctr_object* o) {
	ctr_object* object = NULL;
	ctr_object* context;
	if (CtrStdFlow) return CtrStdNil;
	context = ctr_contexts[ctr_context_id];
	key->info.sticky = 0;
	object = o;
	ctr_internal_object_set_property(context, key, object, 0);
	return object;
}