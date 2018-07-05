# -*- coding: utf-8 -*-
# Generated by Django 1.10.7 on 2018-07-05 22:55
from __future__ import unicode_literals

from django.db import migrations, models


class Migration(migrations.Migration):

    initial = True

    dependencies = [
    ]

    operations = [
        migrations.CreateModel(
            name='Topic',
            fields=[
                ('id', models.AutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('number', models.PositiveIntegerField(blank=True, null=True)),
                ('title', models.CharField(max_length=512)),
                ('seed_query', models.CharField(max_length=512)),
                ('description', models.TextField(blank=True, null=True)),
                ('display_description', models.TextField(blank=True, null=True)),
                ('narrative', models.TextField(blank=True, null=True)),
                ('created_at', models.DateTimeField(auto_now_add=True)),
                ('updated_at', models.DateTimeField(auto_now=True)),
            ],
        ),
    ]