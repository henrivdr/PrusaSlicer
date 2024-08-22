cmake_minimum_required(VERSION 3.13)
project(semver)

add_library(semver STATIC
    semver.c
    semver.h
)

encoding_check(semver)
