"""Synthetic ownership/graph tests; retail files and the game are not accessed."""
import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET
spec = importlib.util.spec_from_file_location('buddy_stage', Path(__file__).resolve().parents[1]/'tools'/'stage_buddy_capacity.py')
M = importlib.util.module_from_spec(spec); spec.loader.exec_module(M)

def fixture(typ='TppHorse2'):
    r=ET.Element('fox', formatVersion='2', fileVersion='0'); es=ET.SubElement(r,'entities')
    def entity(cls, addr, ident):
        e=ET.SubElement(es,'entity', {'class':cls,'classVersion':'0','classID':'0x1','addr':addr,'id':ident})
        ET.SubElement(e,'staticProperties'); ET.SubElement(e,'dynamicProperties'); return e
    def p(e,name,value,kind='EntityPtr'):
        x=ET.SubElement(e.find('staticProperties'),'property',name=name,type=kind,container='StaticArray',arraySize='1')
        c=ET.SubElement(x,'containerEntry');c.text=value;return c
    ds=entity('DataSet','0x10','0x1')
    li=ET.SubElement(ds.find('staticProperties'),'property',name='dataList',type='EntityPtr',container='StringMap',arraySize='2')
    for name,addr in [('BuddyGameObject','0x20'),('BuddyGameObjectLocator','0x30')]:
        c=ET.SubElement(li,'containerEntry',key=name); ET.SubElement(c,'data').text=addr
    g=entity('GameObject','0x20','0x2')
    p(g,'name','BuddyGameObject','String');p(g,'typeName',typ,'String');p(g,'groupId','0','UInt32')
    p(g,'totalCount','1','UInt32');p(g,'realizedCount','1','UInt32');p(g,'partsFile','original.parts','FilePtr')
    l=entity('GameObjectLocator','0x30','0x3')
    p(l,'name','BuddyGameObjectLocator','String');p(l,'typeName',typ,'String');p(l,'groupId','0','UInt32')
    p(l,'dataSet','0x10','EntityHandle');p(l,'transform','0x40');p(l,'parameters','0x50')
    t=entity('TransformEntity','0x40','0x4');p(t,'owner','0x30','EntityHandle')
    c=p(t,'transform_translation',None,'Vector3');c.attrib.update(x='0',y='0',z='0',w='0')
    a=entity(typ+'LocatorParameter','0x50','0x5');p(a,'owner','0x30','EntityHandle')
    p(a,'identifier','', 'String')
    return ET.ElementTree(r),g,l,t,a

class Capacity(unittest.TestCase):
    def test_two_locators_keep_original_and_closed_graph(self):
        tree,g,l,t,a=fixture(); before=ET.tostring(l),ET.tostring(t),ET.tostring(a)
        report=M.expand_xml(tree,'TppHorse2');M.validate_graph(tree)
        self.assertEqual(report['locators'],2);self.assertFalse(report['runtime_index_mapping_verified'])
        self.assertEqual((ET.tostring(l),ET.tostring(t),ET.tostring(a)),before)
        self.assertEqual(M.scalar(g,'totalCount').text,'2');self.assertEqual(M.scalar(g,'realizedCount').text,'2')
        self.assertEqual(M.scalar(g,'partsFile').text,'original.parts')
        locs=tree.findall("./entities/entity[@class='GameObjectLocator']")
        self.assertEqual(len(locs),2);self.assertEqual(M.scalar(locs[1],'name').text,'BuddyGameObjectLocator_coop1')
        self.assertEqual(M.scalar(locs[1],'groupId').text,'0')
    def test_walker_same_graph_rule(self):
        tree,*_=fixture('TppWalkerGear2');self.assertEqual(M.expand_xml(tree,'TppWalkerGear2')['realized'],2)
    def test_capacity_already_changed_refused(self):
        tree,g,*_=fixture();M.scalar(g,'totalCount').text='2'
        with self.assertRaises(ValueError):M.expand_xml(tree,'TppHorse2')
    def test_dangling_graph_refused(self):
        tree,g,l,*_=fixture();M.scalar(l,'parameters').text='0x999'
        with self.assertRaises(ValueError):M.expand_xml(tree,'TppHorse2')
    def test_duplicate_address_refused(self):
        tree,g,l,*_=fixture();l.set('addr',g.get('addr'))
        with self.assertRaises(ValueError):M.expand_xml(tree,'TppHorse2')
    def test_duplicate_id_refused(self):
        tree,g,l,*_=fixture();l.set('id',g.get('id'))
        with self.assertRaises(ValueError):M.expand_xml(tree,'TppHorse2')
    def test_different_type_refused(self):
        tree,*_=fixture()
        with self.assertRaises(ValueError):M.expand_xml(tree,'TppWalkerGear2')
    def test_wrong_owner_refused(self):
        tree,g,l,t,*_=fixture();M.scalar(t,'owner').text=g.get('addr')
        with self.assertRaises(ValueError):M.expand_xml(tree,'TppHorse2')

if __name__=='__main__':unittest.main()
