# -*- coding: utf-8 -*-
"""
Created on Mon Nov 28 15:05:36 2016
# 这个文件用来对预处理之后的文件pre.csv进行机器学习计算，分类结果写入pdf文件，之后计算准确度、召回率、f1值
@author: WangYixin
"""
import pandas as pd
from pandas import Series
import numpy as np
import pydotplus
from sklearn import tree
from sklearn.externals.six import StringIO
from sklearn.metrics import precision_recall_curve
from sklearn.metrics import classification_report
from sklearn.model_selection import train_test_split
from sklearn.metrics import precision_recall_fscore_support as score
import re
# load data
datadic = 'statistics_data.csv'
mydata = pd.read_csv(datadic)
# print "b"
# print mydata
# print type(mydata)
# # prepare
# whetherlist = []
# windlist =[]
# resultlist = []
# for i in range(len(mydata)):
#     if mydata.ix[i][u'天气'] == u'晴':
#         whetherlist.append(0)
#     elif mydata.ix[i][u'天气'] == u'多云':
#         whetherlist.append(1)
#     elif mydata.ix[i][u'天气'] == u'有雨':
#         whetherlist.append(2)
#     if mydata.ix[i][u'风况'] == u'有':
#         windlist.append(1)
#     elif mydata.ix[i][u'风况'] == u'无':
#         windlist.append(0)
#     if mydata.ix[i][u'运动'] == u'适合':
#         resultlist.append(1)
#     elif mydata.ix[i][u'运动'] == u'不适合':
#         resultlist.append(0)
# mydata[u'天气']=Series(whetherlist)#将中文数据数值化
# mydata[u'风况']=Series(windlist)
# mydata[u'运动']=Series(resultlist)


# def precision(y_true, y_pred):
#     i = set(y_true).intersection(y_pred)
#     len1 = len(y_pred)
#     if len1 == 0:
#         return 0
#     else:
#         return len(i) / len1


# def recall(y_true, y_pred):
#     i = set(y_true).intersection(y_pred)
#     return len(i) / len(y_true)


# def f1(y_true, y_pred):
#     p = precision(y_true, y_pred)
#     r = recall(y_true, y_pred)
#     if p + r == 0:
#         return 0
#     else:
#         return 2 * (p * r) / (p + r)


# if __name__ == '__main__':
#     print(f1(['A', 'B', 'C'], ['A', 'B']))

xdata = mydata.values[:, 1:65]
ydata = mydata.values[:, 0]
# print "c"
# print xdata
# print "d"
# print ydata
tmp = ["allpackets_1",
       "noise_1", "active_time_1", "busy_time_1", "receive_time_1",
       "transmit_time_1", "rbytes_1", "packets_1", "qlen_1", "backlogs_1",
       "drops_1", "requeues_1", "retrans_2", "allpackets_2", "noise_2",
       "active_time_2", "busy_time_2", "receive_time_2", "transmit_time_2",
       "rbytes_2", "packets_2", "qlen_2", "backlogs_2", "drops_2",
       "requeues_2", "retrans_3", "allpackets_3", "noise_3",
       "active_time_3", "busy_time_3", "receive_time_3", "transmit_time_3",
       "rbytes_3", "packets_3", "qlen_3",
       "backlogs_3", "drops_3", "requeues_3",
       "retrans_4", "allpackets_4", "noise_4", "active_time_4", "busy_time_4",
       "receive_time_4", "transmit_time_4", "rbytes_4", "packets_4", "qlen_4",
       "backlogs_4", "drops_4", "requeues_4", "retrans_5", "allpackets_5",
       "noise_5", "active_time_5", "busy_time_5", "receive_time_5",
       "transmit_time_5", "rbytes_5", "packets_5", "qlen_5", "backlogs_5",
       "drops_5", "requeues_5"]
# tmp = ("noise", "active_time",
#        "busy_time", "receive_time", "transmit_time",
#        "rbytes", "packets", "qlen",
#        "backlogs", "drops", "requeues")
print len(tmp)
print "1"
# min_sample_split将划分进行到底，如果设置是默认的2的时候将会有一部分划分不准确，但是这样做有过拟合
# 的风险
clf = tree.DecisionTreeClassifier(
    criterion='entropy', min_samples_split=200, min_samples_leaf=400)  # 信息熵作为划分的标准，CART
x_train, x_test, y_train, y_test = train_test_split(
    xdata, ydata, test_size=0.3)
clf = clf.fit(x_train, y_train)
print "2"
dot_data = StringIO()
print "3"

tree.export_graphviz(
    clf, out_file=dot_data,
    feature_names=tmp,
    class_names=['0', '1', '2'],
    filled=True, rounded=True, special_characters=True)
print "4"
graph = pydotplus.graph_from_dot_data(dot_data.getvalue())
print "here"
# 导出
graph.write_pdf('sport.pdf')
graph.write_png('sport.png')
graph.write('abc')
strtree = graph.to_string()
# print strtree
strtree = re.split("\n", strtree)
nodes = []
links = []
for i in strtree:
    # print i
    lief = True
    if i.find("&le") > 0:
        # print "here", i
        lief = False
    i = re.split(" ", i)
    try:
        x = int(i[0])
    except:
        continue
    # print x
    try:
        (x, y, z) = (i[0], i[1], i[2])
        # print x
        x = int(x)
        try:
            z = z.replace(";", "")
            z = int(z)
        except:
            pass
        if y == "->":
            links.append((x, z))
        else:
            if lief is False:
                tmp = str(i)
                index1 = tmp.find("=<") + 2
                index2 = tmp.find("<br/>")
                tmp = tmp[index1:index2]
                tmp = tmp.replace("'", "")
                tmp = re.split(",", tmp)
                # print tmp
                (left, right) = (tmp[0], tmp[2])
                # right = float(right)
                # print left, right
                # links.append((x, z, value))
                nodes.append((x, left, right))
            else:
                nodes.append(x, "BBB", -1000)
    except:
        pass
print nodes
print links
# print len(clf.feature_importances_), len(tmp)
# exit()
for i in range(0, len(tmp)):
    print tmp[i], ':', clf.feature_importances_[i]
# print clf.feature_importances_
# print len(clf.feature_importances_)
answer = clf.predict(x_train)
precision, recall, fscore, support = score(y_train, clf.predict(x_train))
print('precision: {}'.format(precision))
print('recall: {}'.format(recall))
print('fscore: {}'.format(fscore))
print('support: {}'.format(support))
# res = f1(y_train, clf.predict(x_train))
# print res
# print y_train
# precision, recall, thresholds = precision_recall_curve(y_train, clf.predict(x_train)
#                                                        )
# answer = clf.predict_proba(xdata)[:, 1]
# print(classification_report(ydata, answer, target_names=['0', '1', '2', '3']))
