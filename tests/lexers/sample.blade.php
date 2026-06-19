{{-- Blade lexer sample --}}
@extends('layouts.app')
@section('content')
<h1>{{ $title }}</h1>
@if($count > 0)
  <p class="badge">{{ $count }}</p>
@endif
@endsection
